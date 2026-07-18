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

// hook_bus -- server-wide lifecycle hook storage, registration, removal,
// and firing. Carved out of webserver_impl (the state), webserver_add_hook.cpp
// (registration), hook_handle.cpp (removal), and hook_phase_dispatch.cpp
// (firing) so the eleven phase vectors, their shared mutex, the advisory
// gate array, and the two alias slots live behind one cohesive collaborator.
// The coordinator (webserver_impl) forwards fire_* / has_hooks_for /
// phase_hook_count through to this class, binding its log_dispatch_error as
// the per-call error logger.

#include "httpserver/detail/hook_bus.hpp"

#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_response.hpp"

namespace httpserver {
namespace detail {

// ---- registration --------------------------------------------------------

template <class Vec, class Fn>
std::uint64_t hook_bus::add_impl(hook_phase requested, hook_phase expected,
                                 Vec& vec, Fn fn) {
    if (requested != expected) {
        throw std::invalid_argument(
            std::string("hook phase mismatch: add_hook overload for ")
            + std::string(::httpserver::to_string(expected))
            + " received phase tag "
            + std::string(::httpserver::to_string(requested)));
    }
    if (!fn) {
        throw std::invalid_argument("hook callable must not be empty");
    }
    const std::uint64_t id =
        next_slot_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::unique_lock lock(hook_table_mutex_);
        vec.push_back({id, std::move(fn)});
        // Store under the unique_lock. The gate is advisory: readers load
        // it relaxed and take hook_table_mutex_ before touching the
        // vector, so the mutex -- not this store's ordering -- provides
        // the happens-before for the vector contents. See the any_hooks_
        // contract in hook_bus.hpp.
        any_hooks_[static_cast<std::size_t>(expected)].store(
            true, std::memory_order_release);
    }
    return id;
}

std::uint64_t hook_bus::add(hook_phase requested,
        std::function<void(const connection_open_ctx&)> fn) {
    return add_impl(requested, hook_phase::connection_opened,
        hooks_connection_opened_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<void(const accept_ctx&)> fn) {
    return add_impl(requested, hook_phase::accept_decision,
        hooks_accept_decision_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<hook_action(request_received_ctx&)> fn) {
    return add_impl(requested, hook_phase::request_received,
        hooks_request_received_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<hook_action(body_chunk_ctx&)> fn) {
    return add_impl(requested, hook_phase::body_chunk,
        hooks_body_chunk_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<void(const route_resolved_ctx&)> fn) {
    return add_impl(requested, hook_phase::route_resolved,
        hooks_route_resolved_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<hook_action(before_handler_ctx&)> fn) {
    return add_impl(requested, hook_phase::before_handler,
        hooks_before_handler_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    return add_impl(requested, hook_phase::handler_exception,
        hooks_handler_exception_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<hook_action(after_handler_ctx&)> fn) {
    return add_impl(requested, hook_phase::after_handler,
        hooks_after_handler_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<void(const response_sent_ctx&)> fn) {
    return add_impl(requested, hook_phase::response_sent,
        hooks_response_sent_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<void(const request_completed_ctx&)> fn) {
    return add_impl(requested, hook_phase::request_completed,
        hooks_request_completed_, std::move(fn));
}
std::uint64_t hook_bus::add(hook_phase requested,
        std::function<void(const connection_close_ctx&)> fn) {
    return add_impl(requested, hook_phase::connection_closed,
        hooks_connection_closed_, std::move(fn));
}

// ---- removal -------------------------------------------------------------

namespace {

// Route the (phase, slot_id) tuple to the correctly-typed per-phase
// vector and invoke `erase_and_reset` (a generic lambda) on it. Split
// into two helpers (`_lifecycle` / `_handler`) so each stays inside the
// CCN ceiling; the switch arms cannot collapse into a table lookup
// because each phase vector is a differently-typed phase_entry<Sig>.
// The helper names mirror the enum ranges (phase <= route_resolved vs
// the rest), not true lifecycle-vs-handler semantics.
template <class EraseFn>
void erase_slot_for_lifecycle_phase_(hook_bus* self, hook_phase phase,
                                     const EraseFn& erase_and_reset) {
    switch (phase) {
    case hook_phase::connection_opened:
        erase_and_reset(self->hooks_connection_opened_); break;
    case hook_phase::accept_decision:
        erase_and_reset(self->hooks_accept_decision_); break;
    case hook_phase::request_received:
        erase_and_reset(self->hooks_request_received_); break;
    case hook_phase::body_chunk:
        erase_and_reset(self->hooks_body_chunk_); break;
    case hook_phase::route_resolved:
        erase_and_reset(self->hooks_route_resolved_); break;
    default:
        break;
    }
}

template <class EraseFn>
void erase_slot_for_handler_phase_(hook_bus* self, hook_phase phase,
                                   const EraseFn& erase_and_reset) {
    switch (phase) {
    case hook_phase::before_handler:
        erase_and_reset(self->hooks_before_handler_); break;
    case hook_phase::handler_exception:
        // The alias slot (handler_exception_alias_) is immutable after
        // construction, so remove() only touches the user vector here.
        // erase_and_reset re-checks the alias before clearing any_hooks_
        // on an empty user vector, so the gate correctly stays true for
        // as long as the alias is still wired.
        erase_and_reset(self->hooks_handler_exception_); break;
    case hook_phase::after_handler:
        erase_and_reset(self->hooks_after_handler_); break;
    case hook_phase::response_sent:
        erase_and_reset(self->hooks_response_sent_); break;
    case hook_phase::request_completed:
        erase_and_reset(self->hooks_request_completed_); break;
    case hook_phase::connection_closed:
        erase_and_reset(self->hooks_connection_closed_); break;
    default:
        break;
    }
}

template <class EraseFn>
void erase_slot_for_phase(hook_bus* self, hook_phase phase,
                          const EraseFn& erase_and_reset) {
    if (static_cast<int>(phase) <=
            static_cast<int>(hook_phase::route_resolved)) {
        erase_slot_for_lifecycle_phase_(self, phase, erase_and_reset);
        return;
    }
    erase_slot_for_handler_phase_(self, phase, erase_and_reset);
}

}  // namespace

void hook_bus::remove(hook_phase phase, std::uint64_t slot_id) noexcept {
    std::unique_lock lock(hook_table_mutex_);

    // Linear scan for the matching slot, erase if found, and clear the
    // any_hooks_ gate if the vector becomes empty. Phase vectors are tiny
    // in practice (single-digit hook counts). A not-found result is the
    // idempotent no-op case (slot already erased, or never inserted).
    auto erase_and_reset = [slot_id, this, phase](auto& vec) {
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->slot_id == slot_id) {
                vec.erase(it);
                if (vec.empty()) {
                    // Don't clear the gate out from under a still-wired
                    // alias slot: handler_exception_alias_ / log_access_
                    // alias_ are the "still has a hook" signal for their
                    // phases even after the last user-vector entry is
                    // removed. Both slots are write-once at construction
                    // and immutable thereafter, so reading them here
                    // under hook_table_mutex_ needs no extra sync.
                    const bool alias_still_wired =
                        (phase == hook_phase::handler_exception &&
                         handler_exception_alias_) ||
                        (phase == hook_phase::response_sent &&
                         log_access_alias_);
                    if (!alias_still_wired) {
                        any_hooks_[static_cast<std::size_t>(phase)]
                            .store(false, std::memory_order_release);
                    }
                }
                return;
            }
        }
    };

    erase_slot_for_phase(this, phase, erase_and_reset);
}

// ---- alias slots ---------------------------------------------------------

void hook_bus::set_handler_exception_alias(
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    handler_exception_alias_ = std::move(fn);
    // Set the any_hooks_ gate so it remains the canonical zero-cost fast-
    // check for handler_exception, regardless of whether hooks are in the
    // vector or only in the alias slot.
    any_hooks_[static_cast<std::size_t>(hook_phase::handler_exception)]
        .store(true, std::memory_order_release);
}

void hook_bus::set_log_access_alias(
        std::function<void(const response_sent_ctx&)> fn) {
    // NOTE: intentionally does NOT arm any_hooks_[response_sent] --
    // fire_response_sent_gated checks has_log_access_alias() explicitly.
    log_access_alias_ = std::move(fn);
}

// ---- per-phase count -----------------------------------------------------

std::size_t hook_bus::phase_hook_count(hook_phase p) const noexcept {
    if (static_cast<int>(p) <=
            static_cast<int>(hook_phase::route_resolved)) {
        return phase_hook_count_lifecycle_(p);
    }
    return phase_hook_count_handler_(p);
}

std::size_t hook_bus::phase_hook_count_lifecycle_(
        hook_phase p) const noexcept {
    switch (p) {
    case hook_phase::connection_opened:
        return hooks_connection_opened_.size();
    case hook_phase::accept_decision:
        return hooks_accept_decision_.size();
    case hook_phase::request_received:
        return hooks_request_received_.size();
    case hook_phase::body_chunk:
        return hooks_body_chunk_.size();
    case hook_phase::route_resolved:
        return hooks_route_resolved_.size();
    default:
        return 0;
    }
}

std::size_t hook_bus::phase_hook_count_handler_(hook_phase p) const noexcept {
    switch (p) {
    case hook_phase::before_handler:
        return hooks_before_handler_.size();
    case hook_phase::handler_exception:
        return hooks_handler_exception_.size();
    case hook_phase::after_handler:
        return hooks_after_handler_.size();
    case hook_phase::response_sent:
        return hooks_response_sent_.size();
    case hook_phase::request_completed:
        return hooks_request_completed_.size();
    case hook_phase::connection_closed:
        return hooks_connection_closed_.size();
    default:
        return 0;
    }
}

// ---- firing --------------------------------------------------------------
//
// Snapshot the phase vector under a shared_lock, release the lock,
// iterate the snapshot inside a try/catch routed through `on_error`.
// Reentrancy: a hook may call add()/remove() because we no longer hold
// the table lock by the time the user code runs. The snapshot vector is
// thread_local so the per-template-instantiation per-thread buffer is
// reused across calls -- no heap allocation after the first request on
// each thread (warm path).

// One-allocation log helpers shared by both dispatch templates. Free
// helpers (not lambdas captured per-instance) so the two templates' inner
// catch blocks stay one-liners without divergent strings.
namespace {

void log_hook_threw(const hook_bus::log_fn& on_error,
                    std::string_view phase_name, const char* what) {
    on_error(std::string("hook[").append(phase_name)
                 .append("] threw: ").append(what));
}
void log_hook_threw_unknown(const hook_bus::log_fn& on_error,
                            std::string_view phase_name) {
    on_error(std::string("hook[").append(phase_name)
                 .append("] threw unknown exception"));
}
void log_snapshot_failed(const hook_bus::log_fn& on_error,
                         std::string_view template_name,
                         std::string_view phase_name) {
    on_error(std::string(template_name).append("[").append(phase_name)
                 .append("]: snapshot copy failed"));
}

template <typename Ctx>
void fire_hooks_for_phase(
    std::shared_mutex& mtx,
    std::vector<hook_bus::phase_entry<void(const Ctx&)>>& hook_vec,
    const Ctx& ctx,
    std::string_view phase_name,
    const hook_bus::log_fn& on_error) {
    using EntryVec = std::vector<hook_bus::phase_entry<void(const Ctx&)>>;
    try {
        thread_local EntryVec snapshot;
        snapshot.clear();
        {
            std::shared_lock lock(mtx);
            if (hook_vec.empty()) return;  // early-exit: no alloc
            snapshot = hook_vec;
        }
        for (const auto& entry : snapshot) {
            try {
                entry.fn(ctx);
            } catch (const std::exception& e) {
                log_hook_threw(on_error, phase_name, e.what());
            } catch (...) {
                log_hook_threw_unknown(on_error, phase_name);
            }
        }
    } catch (...) {
        // Snapshot copy itself failed (e.g. allocator threw). Nothing
        // more we can do at this layer; callers are noexcept so the
        // contract holds even if std::terminate triggers.
        log_snapshot_failed(on_error, "fire_hooks_for_phase", phase_name);
    }
}

// Short-circuit-capable variant: the entry's std::function returns
// hook_action, the ctx is mutable, and on the first non-pass hook we
// abandon the chain and return the extracted http_response. A throwing
// hook is caught + logged and treated as if it returned pass().
template <typename Ctx>
std::optional<::httpserver::http_response>
fire_short_circuit_hooks_for_phase(
    std::shared_mutex& mtx,
    std::vector<hook_bus::phase_entry<::httpserver::hook_action(Ctx&)>>&
        hook_vec,
    Ctx& ctx,
    std::string_view phase_name,
    const hook_bus::log_fn& on_error) {
    using EntryVec = std::vector<hook_bus::phase_entry<
        ::httpserver::hook_action(Ctx&)>>;
    try {
        thread_local EntryVec snapshot;
        snapshot.clear();
        {
            std::shared_lock lock(mtx);
            if (hook_vec.empty()) return std::nullopt;  // early-exit
            snapshot = hook_vec;
        }
        for (auto& entry : snapshot) {
            try {
                auto action = entry.fn(ctx);
                if (!action.is_pass()) {
                    return std::move(action).take_response();
                }
            } catch (const std::exception& e) {
                log_hook_threw(on_error, phase_name, e.what());
            } catch (...) {
                log_hook_threw_unknown(on_error, phase_name);
            }
        }
    } catch (...) {
        log_snapshot_failed(on_error, "fire_short_circuit_hooks_for_phase",
                            phase_name);
    }
    return std::nullopt;
}

}  // namespace

// ---- fire_*: lifecycle (void, observation) -------------------------------

void hook_bus::fire_connection_opened(
    const connection_open_ctx& ctx, const log_fn& on_error) noexcept {
    fire_hooks_for_phase(hook_table_mutex_, hooks_connection_opened_, ctx,
                         "connection_opened", on_error);
}

void hook_bus::fire_accept_decision(
    const accept_ctx& ctx, const log_fn& on_error) noexcept {
    fire_hooks_for_phase(hook_table_mutex_, hooks_accept_decision_, ctx,
                         "accept_decision", on_error);
}

void hook_bus::fire_connection_closed(
    const connection_close_ctx& ctx, const log_fn& on_error) noexcept {
    fire_hooks_for_phase(hook_table_mutex_, hooks_connection_closed_, ctx,
                         "connection_closed", on_error);
}

// ---- fire_*: pre-handler short-circuit -----------------------------------

std::optional<http_response> hook_bus::fire_request_received(
    request_received_ctx& ctx, const log_fn& on_error) noexcept {
    return fire_short_circuit_hooks_for_phase(
        hook_table_mutex_, hooks_request_received_, ctx,
        "request_received", on_error);
}

std::optional<http_response> hook_bus::fire_body_chunk(
    body_chunk_ctx& ctx, const log_fn& on_error) noexcept {
    return fire_short_circuit_hooks_for_phase(
        hook_table_mutex_, hooks_body_chunk_, ctx, "body_chunk", on_error);
}

// ---- fire_*: route_resolved + before_handler -----------------------------

void hook_bus::fire_route_resolved(
    const route_resolved_ctx& ctx, const log_fn& on_error) noexcept {
    fire_hooks_for_phase(hook_table_mutex_, hooks_route_resolved_, ctx,
                         "route_resolved", on_error);
}

std::optional<http_response> hook_bus::fire_before_handler(
    before_handler_ctx& ctx, const log_fn& on_error) noexcept {
    return fire_short_circuit_hooks_for_phase(
        hook_table_mutex_, hooks_before_handler_, ctx,
        "before_handler", on_error);
}

// ---- fire_*: handler_exception -------------------------------------------
//
// handler_exception is the only short-circuit-capable phase whose ctx is
// passed as `const&` (the user cannot mutate the in-flight exception or
// request), and it layers a dedicated single-slot alias on top of the
// user vector (handler_exception_alias_ -- the v1 internal_error_handler
// re-described as a last-position hook). Both make the body awkward to
// express via fire_short_circuit_hooks_for_phase<Ctx&>; the firing logic
// is inlined here. The body mirrors the template's structure with an
// extra tail that invokes the alias slot after the user vector.
std::optional<http_response> hook_bus::fire_handler_exception(
    const handler_exception_ctx& ctx, const log_fn& on_error) noexcept {
    using EntryVec = std::vector<phase_entry<
        hook_action(const handler_exception_ctx&)>>;
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
                log_hook_threw(on_error, "handler_exception", e.what());
            } catch (...) {
                log_hook_threw_unknown(on_error, "handler_exception");
            }
        }
    } catch (...) {
        on_error("fire_handler_exception: snapshot copy failed");
    }
    // Tail: invoke the alias slot, if any. Read without synchronisation:
    // the slot is single-writer-at-construction, immutable after start().
    //
    // Throw containment: a throwing alias is logged with the legacy
    // "internal_error_handler threw" prefix so the error-log contract
    // (and its tests in basic.cpp) is preserved verbatim.
    if (handler_exception_alias_) {
        try {
            auto action = handler_exception_alias_(ctx);
            if (!action.is_pass()) {
                return std::move(action).take_response();
            }
        } catch (const std::exception& e) {
            on_error(std::string("internal_error_handler threw: ")
                         .append(e.what()));
        } catch (...) {
            on_error("internal_error_handler threw unknown exception");
        }
    }
    return std::nullopt;
}

// ---- fire_*: post-handler ------------------------------------------------

std::optional<http_response> hook_bus::fire_after_handler(
    after_handler_ctx& ctx, const log_fn& on_error) noexcept {
    return fire_short_circuit_hooks_for_phase(
        hook_table_mutex_, hooks_after_handler_, ctx,
        "after_handler", on_error);
}

// response_sent: void-returning user hooks, then the log_access_alias_
// slot. Same alias-tail pattern as fire_handler_exception.
void hook_bus::fire_response_sent(
    const response_sent_ctx& ctx, const log_fn& on_error) noexcept {
    fire_hooks_for_phase(hook_table_mutex_, hooks_response_sent_, ctx,
                         "response_sent", on_error);
    if (log_access_alias_) {
        try {
            log_access_alias_(ctx);
        } catch (const std::exception& e) {
            on_error(std::string("log_access alias threw: ")
                         .append(e.what()));
        } catch (...) {
            on_error("log_access alias threw unknown exception");
        }
    }
}

// request_completed: unconditional final hook. Pure observation -- no
// alias slot here (the v1 log_access setter aliases to response_sent).
void hook_bus::fire_request_completed(
    const request_completed_ctx& ctx, const log_fn& on_error) noexcept {
    fire_hooks_for_phase(hook_table_mutex_, hooks_request_completed_, ctx,
                         "request_completed", on_error);
}

}  // namespace detail
}  // namespace httpserver
