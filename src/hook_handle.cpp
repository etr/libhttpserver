/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

// TASK-045 -- Out-of-line bodies for hook_handle and peer_address.
//
// hook_handle's destructor + remove() + move ops need the complete
// type of detail::webserver_impl (the per-phase vectors live there),
// so the bodies live here, not in the public header. Same pattern as
// http_response's move/dtor.

#include "httpserver/hook_handle.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/hook_action.hpp"
#include "httpserver/http_response.hpp"

#include "httpserver/hook_context.hpp"
#include "httpserver/detail/resource_hook_table.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// ---- hook_handle ---------------------------------------------------------

hook_handle::hook_handle(hook_handle&& other) noexcept
    : impl_(other.impl_),
      slot_id_(other.slot_id_),
      phase_(other.phase_),
      armed_(other.armed_),
      table_weak_(std::move(other.table_weak_)) {
    // Disarm the source so its destructor is a no-op.
    other.armed_ = false;
    other.impl_ = nullptr;
    other.table_weak_.reset();
}

hook_handle& hook_handle::operator=(hook_handle&& other) noexcept {
    if (this != &other) {
        // Remove any registration this object currently owns before
        // taking over the source's. remove() is idempotent and
        // noexcept, so this is safe even if `armed_` is already false.
        remove();
        impl_ = other.impl_;
        slot_id_ = other.slot_id_;
        phase_ = other.phase_;
        armed_ = other.armed_;
        table_weak_ = std::move(other.table_weak_);
        other.armed_ = false;
        other.impl_ = nullptr;
        other.table_weak_.reset();
    }
    return *this;
}

hook_handle::~hook_handle() {
    if (armed_) {
        remove();
    }
}

// TASK-051: drain a per-route registration. Called by hook_handle::remove
// when impl_ == nullptr (handles produced by http_resource::add_hook).
// Disarm BEFORE the table call mirrors the server-wide discipline in
// hook_handle::remove. If the resource owning the table has been
// destroyed, the weak_ptr is expired and this is a no-op (the action-item
// contract: "if the resource is destroyed before the handle, remove() is
// a no-op").
static void remove_per_route(std::weak_ptr<detail::resource_hook_table>& weak,
                             hook_phase phase,
                             std::uint64_t slot_id) noexcept {
    auto table = weak.lock();
    weak.reset();
    if (table) {
        table->remove_slot(phase, slot_id);
    }
}

void hook_handle::remove() noexcept {
    if (!armed_) {
        return;
    }
    // TASK-051: handles produced by http_resource::add_hook have
    // impl_ == nullptr and carry a (possibly-expired) weak_ptr to
    // the resource's hook table. Handles produced by webserver::add_hook
    // have a non-null impl_ and a default-constructed (empty) weak_ptr.
    if (impl_ == nullptr) {
        const auto phase = phase_;
        const auto id = slot_id_;
        armed_ = false;
        remove_per_route(table_weak_, phase, id);
        return;
    }
    // Snapshot the source state and disarm BEFORE doing the erase, so
    // any exception from the lambda's destructor (very unlikely, but
    // remove() is declared noexcept and std::terminate is the
    // observable outcome anyway) does not leave the handle in a
    // half-removed state.
    auto* impl = impl_;
    const auto phase = phase_;
    const auto id = slot_id_;
    armed_ = false;
    impl_ = nullptr;

    std::unique_lock lock(impl->hook_table_mutex_);

    // Linear scan for the matching slot. Phase vectors are tiny in
    // practice (single-digit hook counts). A not-found result is the
    // idempotent no-op case: the slot was already erased by an earlier
    // remove() or never inserted (defensive).
    //
    // erase_if_found returns true iff the slot was found and erased.
    // We use the return value to skip reset_gate_if_empty on a no-op
    // erase (the gate value is already consistent -- no erase happened).
    // A false return from an armed handle would indicate a bug in
    // register_hook_impl (slot never inserted); an assert catches this in
    // debug builds. The !armed_ early-return above ensures that a second
    // remove() on the same handle never reaches this code path.
    // erase_and_reset: linear scan for the slot, erase if found, and
    // clear the any_hooks_ gate if the vector becomes empty. The two
    // operations stay together so a new phase only adds a one-line
    // switch arm. Phase vectors are tiny in practice (single-digit
    // hook counts), so linear scan is the right shape here.
    auto erase_and_reset = [id, impl, phase](auto& vec) {
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->slot_id == id) {
                vec.erase(it);
                if (vec.empty()) {
                    impl->any_hooks_[static_cast<std::size_t>(phase)].store(
                        false, std::memory_order_release);
                }
                return;
            }
        }
    };

    // The switch arms below cannot collapse into a table lookup because
    // each phase vector is a differently-typed phase_entry<Sig> — a
    // type-erased unification would either need std::visit over a
    // tuple-of-vectors or boxing each callable, both of which add
    // indirection on the hot path. One arm per phase is the safest
    // shape until the phase set stabilises post-TASK-051.
    switch (phase) {
    case hook_phase::connection_opened:
        erase_and_reset(impl->hooks_connection_opened_); break;
    case hook_phase::accept_decision:
        erase_and_reset(impl->hooks_accept_decision_); break;
    case hook_phase::request_received:
        erase_and_reset(impl->hooks_request_received_); break;
    case hook_phase::body_chunk:
        erase_and_reset(impl->hooks_body_chunk_); break;
    case hook_phase::route_resolved:
        erase_and_reset(impl->hooks_route_resolved_); break;
    case hook_phase::before_handler:
        erase_and_reset(impl->hooks_before_handler_); break;
    case hook_phase::handler_exception:
        // Finding #6 (forward-looking): if a future task adds a runtime
        // re-registration path for handler_exception_alias_, remove()
        // will also need a path to clear that slot and re-evaluate
        // any_hooks_. Currently any_hooks_ remains true while the alias
        // is wired; removing only a user-vector entry does not clear it
        // if the alias is still set. Add that handling alongside the
        // runtime setter.
        erase_and_reset(impl->hooks_handler_exception_); break;
    case hook_phase::after_handler:
        erase_and_reset(impl->hooks_after_handler_); break;
    case hook_phase::response_sent:
        erase_and_reset(impl->hooks_response_sent_); break;
    case hook_phase::request_completed:
        erase_and_reset(impl->hooks_request_completed_); break;
    case hook_phase::connection_closed:
        erase_and_reset(impl->hooks_connection_closed_); break;
    case hook_phase::count_:
        // Unreachable: an armed handle always carries a valid phase.
        break;
    }
}

hook_handle hook_handle::detach() && noexcept {
    // Construct a disarmed handle from the same data; do NOT call the
    // private armed-ctor because the spec says detach() leaves the
    // underlying registration in place (no impl interaction).
    hook_handle out;
    out.impl_ = impl_;
    out.slot_id_ = slot_id_;
    out.phase_ = phase_;
    // detached: registration persists in the phase vector; destructor
    // is disabled. This disarmed state is semantically distinct from a
    // default-constructed (never-registered) handle: both have armed_==false,
    // but here the backing slot is intentionally left alive.
    out.armed_ = false;
    out.table_weak_ = table_weak_;
    // Disarm the source so its destructor is also a no-op.
    armed_ = false;
    impl_ = nullptr;
    table_weak_.reset();
    return out;
}

// ---- peer_address::to_string ---------------------------------------------
//
// Moved to src/peer_address.cpp in TASK-051. See the rationale comment
// at the top of that file.

// ---- fire_* (TASK-046) ---------------------------------------------------
//
// Per the plan: snapshot the phase vector under a shared_lock, release
// the lock, iterate the snapshot inside a try/catch routed through
// log_dispatch_error. Mirrors the TASK-027 route-cache promotion pattern
// (shared_lock + atomic gate). Reentrancy: a hook may call
// ws.add_hook() / handle.remove() because we no longer hold the table
// lock by the time the user code runs.

// (Previously an unused kHookSnapshotReserve constant lived here.
// The thread_local snapshot buffers below are now sized lazily on
// first use; the constant was dead code as of TASK-048's perf rework.
// Removed in TASK-049 to silence -Wunused-const-variable under
// --enable-debug.)

// fire_hooks_for_phase: shared dispatch template for all void-returning
// lifecycle hook phases. Snapshots the caller-supplied vector under a
// shared_lock, releases the lock, then iterates the snapshot invoking
// each hook inside an inner try/catch. Any exception is routed through
// log_dispatch_error. The outer try/catch absorbs snapshot-copy failures
// (e.g. std::bad_alloc). The function is not itself noexcept because the
// callers are noexcept and will std::terminate on uncaught throws; the
// inner catches ensure no exception can propagate.
//
// TASK-048 perf: the snapshot vector is thread_local so the per-template-
// instantiation per-thread buffer is reused across calls — no heap
// allocation after the first request on each thread (warm path).

// One-allocation log helpers shared by fire_hooks_for_phase and
// fire_short_circuit_hooks_for_phase (TASK-049 review). Free helpers
// (not lambdas captured per-instance) so the two templates' inner
// catch blocks can stay one-liners without divergent strings.
static void log_hook_threw(detail::webserver_impl* impl,
                           std::string_view phase_name,
                           const char* what) {
    impl->log_dispatch_error(
        std::string("hook[").append(phase_name)
            .append("] threw: ").append(what));
}
static void log_hook_threw_unknown(detail::webserver_impl* impl,
                                   std::string_view phase_name) {
    impl->log_dispatch_error(
        std::string("hook[").append(phase_name)
            .append("] threw unknown exception"));
}
static void log_snapshot_failed(detail::webserver_impl* impl,
                                std::string_view template_name,
                                std::string_view phase_name) {
    impl->log_dispatch_error(
        std::string(template_name).append("[").append(phase_name)
            .append("]: snapshot copy failed"));
}

template <typename Ctx>
static void fire_hooks_for_phase(
    detail::webserver_impl* impl,
    std::vector<detail::webserver_impl::phase_entry<void(const Ctx&)>>& hook_vec,
    const Ctx& ctx,
    std::string_view phase_name) {
    using EntryVec = std::vector<detail::webserver_impl::phase_entry<void(const Ctx&)>>;
    try {
        thread_local EntryVec snapshot;
        snapshot.clear();
        {
            std::shared_lock lock(impl->hook_table_mutex_);
            if (hook_vec.empty()) return;  // early-exit: no hooks, no allocation
            snapshot = hook_vec;
        }
        for (const auto& entry : snapshot) {
            try {
                entry.fn(ctx);
            } catch (const std::exception& e) {
                log_hook_threw(impl, phase_name, e.what());
            } catch (...) {
                log_hook_threw_unknown(impl, phase_name);
            }
        }
    } catch (...) {
        // Snapshot copy itself failed (e.g. allocator threw). Nothing
        // more we can do at this layer; callers are noexcept so the
        // contract holds even if std::terminate triggers.
        log_snapshot_failed(impl, "fire_hooks_for_phase", phase_name);
    }
}

void detail::webserver_impl::fire_connection_opened(
    const ::httpserver::connection_open_ctx& ctx) noexcept {
    fire_hooks_for_phase(this, hooks_connection_opened_, ctx, "connection_opened");
}

void detail::webserver_impl::fire_accept_decision(
    const ::httpserver::accept_ctx& ctx) noexcept {
    fire_hooks_for_phase(this, hooks_accept_decision_, ctx, "accept_decision");
}

void detail::webserver_impl::fire_connection_closed(
    const ::httpserver::connection_close_ctx& ctx) noexcept {
    fire_hooks_for_phase(this, hooks_connection_closed_, ctx, "connection_closed");
}

// ---- fire_* (TASK-047) ---------------------------------------------------
//
// Short-circuit-capable firing helpers. Mirrors fire_hooks_for_phase but:
//   - the entry's std::function returns hook_action,
//   - the ctx is mutable (Ctx&, not const Ctx&),
//   - on the first non-pass hook we abandon the chain and return the
//     extracted http_response.
//
// A throwing hook is caught + logged and treated as if it had returned
// hook_action::pass() -- same DR-009 §5.2 routing as the void variant.
//
// TASK-048 perf: thread_local snapshot buffer (same rationale as
// fire_hooks_for_phase above).
template <typename Ctx>
static std::optional<::httpserver::http_response>
fire_short_circuit_hooks_for_phase(
    detail::webserver_impl* impl,
    std::vector<detail::webserver_impl::phase_entry<
        ::httpserver::hook_action(Ctx&)>>& hook_vec,
    Ctx& ctx,
    std::string_view phase_name) {
    using EntryVec = std::vector<detail::webserver_impl::phase_entry<
        ::httpserver::hook_action(Ctx&)>>;
    try {
        thread_local EntryVec snapshot;
        snapshot.clear();
        {
            std::shared_lock lock(impl->hook_table_mutex_);
            if (hook_vec.empty()) return std::nullopt;  // early-exit: no hooks, no allocation
            snapshot = hook_vec;
        }
        for (auto& entry : snapshot) {
            try {
                auto action = entry.fn(ctx);
                if (!action.is_pass()) {
                    return std::move(action).take_response();
                }
            } catch (const std::exception& e) {
                log_hook_threw(impl, phase_name, e.what());
            } catch (...) {
                log_hook_threw_unknown(impl, phase_name);
            }
        }
    } catch (...) {
        log_snapshot_failed(impl, "fire_short_circuit_hooks_for_phase",
                            phase_name);
    }
    return std::nullopt;
}

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_request_received(
    ::httpserver::request_received_ctx& ctx) noexcept {
    return fire_short_circuit_hooks_for_phase(
        this, hooks_request_received_, ctx, "request_received");
}

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_body_chunk(
    ::httpserver::body_chunk_ctx& ctx) noexcept {
    return fire_short_circuit_hooks_for_phase(
        this, hooks_body_chunk_, ctx, "body_chunk");
}

// ---- fire_* (TASK-048) ---------------------------------------------------

void detail::webserver_impl::fire_route_resolved(
    const ::httpserver::route_resolved_ctx& ctx) noexcept {
    fire_hooks_for_phase(this, hooks_route_resolved_, ctx, "route_resolved");
}

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_before_handler(
    ::httpserver::before_handler_ctx& ctx) noexcept {
    return fire_short_circuit_hooks_for_phase(
        this, hooks_before_handler_, ctx, "before_handler");
}

// ---- fire_* (TASK-049) ---------------------------------------------------
//
// handler_exception is the only short-circuit-capable phase whose ctx is
// passed as `const&` (the user cannot mutate the in-flight exception or
// request). It also layers a dedicated single-slot alias on top of the
// user vector (handler_exception_alias_ -- the v1 internal_error_handler
// re-described as a last-position hook). Both of those make the body
// awkward to express via fire_short_circuit_hooks_for_phase<Ctx&>; the
// firing logic is inlined here. The body mirrors the template's
// structure -- snapshot under shared_lock, release, iterate with per-hook
// try/catch -- with an extra tail that invokes the alias slot after the
// user vector is exhausted.
//
// Structural differences preventing template reuse (findings #12, #13):
//   1. ctx is `const Ctx&` (not `Ctx&`) -- the template takes mutable.
//   2. The alias tail (handler_exception_alias_) sits AFTER the user
//      vector and has its own throw-containment block.
// If the template is extended to support const-ctx or alias-tail in a
// future task, fire_handler_exception should be collapsed into it and
// the rationale comment updated accordingly.
std::optional<::httpserver::http_response>
detail::webserver_impl::fire_handler_exception(
    const ::httpserver::handler_exception_ctx& ctx) noexcept {
    using EntryVec = std::vector<phase_entry<
        ::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)>>;
    // Per-thread cost note (finding #30): this thread_local EntryVec is a
    // SECOND per-thread snapshot buffer in addition to the one inside
    // fire_short_circuit_hooks_for_phase. Both buffers are warm after the
    // first invocation on a given thread (no heap allocation on subsequent
    // calls). If per-thread memory becomes a concern, a shared
    // thread_local typed-union could unify them, but at typical
    // concurrency levels the extra few KB is negligible.
    try {
        thread_local EntryVec snapshot;
        snapshot.clear();
        {
            std::shared_lock lock(hook_table_mutex_);
            snapshot = hooks_handler_exception_;
        }
        for (auto& entry : snapshot) {
            try {
                auto action = entry.fn(ctx);
                if (!action.is_pass()) {
                    return std::move(action).take_response();
                }
            } catch (const std::exception& e) {
                // Use .append() to avoid two intermediate heap strings.
                log_dispatch_error(
                    std::string("hook[handler_exception] threw: ")
                        .append(e.what()));
            } catch (...) {
                log_dispatch_error(
                    "hook[handler_exception] threw unknown exception");
            }
        }
    } catch (...) {
        log_dispatch_error(
            "fire_handler_exception: snapshot copy failed");
    }
    // Tail: invoke the alias slot, if any. Read without synchronisation;
    // the slot is single-writer-at-construction (see webserver_impl.hpp).
    //
    // Finding #33 (security-reviewer): if a future task adds a runtime
    // setter for handler_exception_alias_, this read MUST be upgraded to
    // take hook_table_mutex_ shared and the writer MUST take it exclusive.
    // The synchronisation contract lives in webserver_impl.hpp at the
    // handler_exception_alias_ field declaration -- keep both sites in
    // sync when adding any runtime re-registration path.
    //
    // Throw containment: a throwing alias is logged with the legacy
    // "internal_error_handler threw" prefix so the DR-009 §5.2 point 4
    // log contract (and its tests in basic.cpp) is preserved verbatim
    // even though the call site has moved from
    // run_internal_error_handler_safely into the hook chain.
    if (handler_exception_alias_) {
        try {
            auto action = handler_exception_alias_(ctx);
            if (!action.is_pass()) {
                return std::move(action).take_response();
            }
        } catch (const std::exception& e) {
            // Use .append() to avoid two intermediate heap strings.
            log_dispatch_error(
                std::string("internal_error_handler threw: ").append(e.what()));
        } catch (...) {
            log_dispatch_error(
                "internal_error_handler threw unknown exception");
        }
    }
    return std::nullopt;
}

// ---- fire_* (TASK-050) ---------------------------------------------------
//
// after_handler is the post-handler short-circuit. Returns engaged
// optional iff a hook short-circuited with respond_with(); the caller
// (fire_after_handler_gated in webserver_finalize.cpp) emplaces the new
// response into mr->response ahead of materialize_and_queue_response.

std::optional<::httpserver::http_response>
detail::webserver_impl::fire_after_handler(
    ::httpserver::after_handler_ctx& ctx) noexcept {
    return fire_short_circuit_hooks_for_phase(
        this, hooks_after_handler_, ctx, "after_handler");
}

// response_sent: void-returning user hooks, then the log_access_alias_
// slot. Same alias-tail pattern as fire_handler_exception (TASK-049).
void detail::webserver_impl::fire_response_sent(
    const ::httpserver::response_sent_ctx& ctx) noexcept {
    fire_hooks_for_phase(this, hooks_response_sent_, ctx, "response_sent");
    // Tail: invoke the alias slot, if any. Read without synchronisation;
    // the slot is single-writer-at-construction (see webserver_impl.hpp).
    if (log_access_alias_) {
        try {
            log_access_alias_(ctx);
        } catch (const std::exception& e) {
            log_dispatch_error(
                std::string("log_access alias threw: ").append(e.what()));
        } catch (...) {
            log_dispatch_error(
                "log_access alias threw unknown exception");
        }
    }
}

// request_completed: unconditional final hook. Pure observation -- no
// alias slot here (the v1 log_access setter aliases to response_sent,
// not request_completed).
void detail::webserver_impl::fire_request_completed(
    const ::httpserver::request_completed_ctx& ctx) noexcept {
    fire_hooks_for_phase(this, hooks_request_completed_, ctx,
                         "request_completed");
}

}  // namespace httpserver
