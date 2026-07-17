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

// hook_phase_dispatch.cpp -- per-phase hook firing (the fire_* family).
//
// Split out of src/hook_handle.cpp to keep both translation units under
// the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). hook_handle.cpp retains the hook_handle
// lifetime ops (move/dtor/detach/remove and the per-phase erase
// templates); this file holds the dispatch helpers and every
// detail::webserver_impl::fire_* member.

#include <exception>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "httpserver/hook_action.hpp"
#include "httpserver/http_response.hpp"

#include "httpserver/hook_context.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// ---- fire_*: lifecycle phases --------------------------------------------
//
// Snapshot the phase vector under a shared_lock, release the lock,
// iterate the snapshot inside a try/catch routed through
// log_dispatch_error. Reentrancy: a hook may call ws.add_hook() /
// handle.remove() because we no longer hold the table lock by the time
// the user code runs.

// fire_hooks_for_phase: shared dispatch template for all void-returning
// lifecycle hook phases. Snapshots the caller-supplied vector under a
// shared_lock, releases the lock, then iterates the snapshot invoking
// each hook inside an inner try/catch. Any exception is routed through
// log_dispatch_error. The outer try/catch absorbs snapshot-copy failures
// (e.g. std::bad_alloc). The function is not itself noexcept because the
// callers are noexcept and will std::terminate on uncaught throws; the
// inner catches ensure no exception can propagate.
//
// Perf: the snapshot vector is thread_local so the per-template-
// instantiation per-thread buffer is reused across calls — no heap
// allocation after the first request on each thread (warm path).

// One-allocation log helpers shared by fire_hooks_for_phase and
// fire_short_circuit_hooks_for_phase. Free helpers
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

// ---- fire_*: pre-handler short-circuit phases ----------------------------
//
// Short-circuit-capable firing helpers. Mirrors fire_hooks_for_phase but:
//   - the entry's std::function returns hook_action,
//   - the ctx is mutable (Ctx&, not const Ctx&),
//   - on the first non-pass hook we abandon the chain and return the
//     extracted http_response.
//
// A throwing hook is caught + logged and treated as if it had returned
// hook_action::pass() -- same exception routing as the void variant.
//
// Perf: thread_local snapshot buffer (same rationale as
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

// ---- fire_*: route_resolved + before_handler -----------------------------

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

// ---- fire_*: handler_exception -------------------------------------------
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
// Structural differences vs fire_short_circuit_hooks_for_phase:
//   1. ctx is `const Ctx&` (not `Ctx&`) -- the template takes mutable.
//   2. The alias tail (handler_exception_alias_) sits AFTER the user
//      vector and has its own throw-containment block.
//   3. No empty-vector early-return under the lock: the template bails
//      out before copying when the phase vector is empty; here the
//      snapshot copy runs unconditionally. Behaviourally irrelevant --
//      copying an empty vector into the already-cleared thread_local
//      buffer allocates nothing, and control must fall through to the
//      alias tail regardless (it fires even with zero user hooks).
// If the template is extended to support const-ctx or alias-tail in a
// future task, fire_handler_exception should be collapsed into it and
// the rationale comment updated accordingly.
std::optional<::httpserver::http_response>
detail::webserver_impl::fire_handler_exception(
    const ::httpserver::handler_exception_ctx& ctx) noexcept {
    using EntryVec = std::vector<phase_entry<
        ::httpserver::hook_action(
            const ::httpserver::handler_exception_ctx&)>>;
    // Per-thread cost note: this thread_local EntryVec is a
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
                log_hook_threw(this, "handler_exception", e.what());
            } catch (...) {
                log_hook_threw_unknown(this, "handler_exception");
            }
        }
    } catch (...) {
        log_dispatch_error(
            "fire_handler_exception: snapshot copy failed");
    }
    // Tail: invoke the alias slot, if any. Read without synchronisation:
    // the slot is single-writer-at-construction, immutable after
    // webserver::start().
    //
    // Throw containment: a throwing alias is logged with the legacy
    // "internal_error_handler threw" prefix so the error-log contract
    // (and its tests in basic.cpp) is preserved verbatim even though
    // the call site has moved from run_internal_error_handler_safely
    // into the hook chain.
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

// ---- fire_*: post-handler phases -----------------------------------------
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
// slot. Same alias-tail pattern as fire_handler_exception.
void detail::webserver_impl::fire_response_sent(
    const ::httpserver::response_sent_ctx& ctx) noexcept {
    fire_hooks_for_phase(this, hooks_response_sent_, ctx, "response_sent");
    // Tail: invoke the alias slot, if any. Read without synchronisation:
    // the slot is single-writer-at-construction, immutable after
    // webserver::start().
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
