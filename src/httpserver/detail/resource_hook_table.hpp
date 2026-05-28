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

// TASK-051: per-resource hook bus storage. Internal header; only
// reachable when compiling libhttpserver translation units. NOT part
// of the installed surface; consumers cannot reach it through the
// public umbrella.
#if !defined(HTTPSERVER_COMPILATION)
#error "resource_hook_table.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_RESOURCE_HOOK_TABLE_HPP_
#define SRC_HTTPSERVER_DETAIL_RESOURCE_HOOK_TABLE_HPP_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"

namespace httpserver {

class http_response;

namespace detail {

// resource_hook_table -- per-http_resource hook storage. Only the five
// post-route-resolution phases are represented (before_handler,
// handler_exception, after_handler, response_sent, request_completed);
// the other six phases are server-scope only (validated at the
// http_resource::add_hook entry point).
//
// Lock discipline (architecture §5.6):
//   route_table_mutex_ (dispatch, shared)  ->
//     resource hook_table_mutex_ (this class, shared during firing)  ->
//       webserver hook_table_mutex_ (server-wide vectors)
// Firing helpers snapshot the relevant vector under a shared_lock,
// release the lock, then iterate the snapshot. Re-entrant add_hook /
// remove() calls from inside a hook are therefore safe (the writer
// path takes the unique_lock only after the firing path has released).
class resource_hook_table {
 public:
    // Slot entry: monotonically increasing id + the callable. Mirrors
    // webserver_impl::phase_entry<Sig> but lives in this class so the
    // public umbrella stays free of <functional>/<vector> transitives.
    template <class Sig>
    struct entry {
        std::uint64_t slot_id;
        std::function<Sig> fn;
    };

    resource_hook_table() = default;
    resource_hook_table(const resource_hook_table&) = delete;
    resource_hook_table& operator=(const resource_hook_table&) = delete;
    resource_hook_table(resource_hook_table&&) = delete;
    resource_hook_table& operator=(resource_hook_table&&) = delete;
    ~resource_hook_table() = default;

    // Append a slot. The expected_phase argument is the phase the
    // calling overload binds to (used to update the any_hooks_ gate).
    // Returns the freshly-allocated slot_id.
    std::uint64_t append_before_handler(
        std::function<hook_action(before_handler_ctx&)> fn);
    std::uint64_t append_handler_exception(
        std::function<hook_action(const handler_exception_ctx&)> fn);
    std::uint64_t append_after_handler(
        std::function<hook_action(after_handler_ctx&)> fn);
    std::uint64_t append_response_sent(
        std::function<void(const response_sent_ctx&)> fn);
    std::uint64_t append_request_completed(
        std::function<void(const request_completed_ctx&)> fn);

    // Remove the slot with `slot_id` from the `phase`'s vector. Not-found
    // is the idempotent no-op path (matches webserver_impl::remove
    // semantics). Re-evaluates the any_hooks_ gate against vector
    // emptiness after removal.
    void remove_slot(hook_phase phase, std::uint64_t slot_id) noexcept;

    // Relaxed-load any_hooks_ gate, used by the dispatch hot path to
    // skip the firing helper entirely when no hooks are registered.
    bool any_hooks(hook_phase phase) const noexcept {
        return any_hooks_[static_cast<std::size_t>(phase)]
            .load(std::memory_order_relaxed);
    }

    // Firing helpers. Each takes a logger callback to route exceptions
    // through webserver_impl::log_dispatch_error -- the table itself has
    // no back-pointer to the server.
    using log_fn = std::function<void(std::string_view)>;

    std::optional<http_response>
    fire_before_handler(before_handler_ctx& ctx, const log_fn& log);
    std::optional<http_response>
    fire_handler_exception(const handler_exception_ctx& ctx, const log_fn& log);
    std::optional<http_response>
    fire_after_handler(after_handler_ctx& ctx, const log_fn& log);
    void fire_response_sent(const response_sent_ctx& ctx, const log_fn& log);
    void fire_request_completed(const request_completed_ctx& ctx,
                                const log_fn& log);

 private:
    // One shared_mutex covering all five vectors -- writes are rare
    // (hook registration / unregistration). Reads (firing) take shared
    // ownership briefly to snapshot the vector.
    mutable std::shared_mutex hook_table_mutex_;
    std::atomic<std::uint64_t> next_slot_id_{1};

    // any_hooks_ gate: one slot per phase value. Only the five permitted
    // phase positions are ever flipped to true; the others stay false
    // forever. Indexed by static_cast<std::size_t>(hook_phase) so the
    // dispatch hot path's lookup is a plain array index. The six unused
    // slots (for the pre-route phases) are always false -- the array
    // shape trades a small amount of memory for constant-time indexed access.
    std::array<std::atomic<bool>,
               static_cast<std::size_t>(hook_phase::count_)> any_hooks_{};

    // Five named vectors, one per permitted phase. The spec described the
    // primary design as a single std::array<std::vector<...>, count_>; the
    // implementation uses named vectors instead for type safety at the
    // append_*/fire_* call sites (each vector's element type differs), at
    // the cost of six empty-but-unused conceptual slots (the pre-route
    // phases are never represented here). See DR-012 "Per-route storage".
    std::vector<entry<hook_action(before_handler_ctx&)>>
        hooks_before_handler_;
    std::vector<entry<hook_action(const handler_exception_ctx&)>>
        hooks_handler_exception_;
    std::vector<entry<hook_action(after_handler_ctx&)>>
        hooks_after_handler_;
    std::vector<entry<void(const response_sent_ctx&)>>
        hooks_response_sent_;
    std::vector<entry<void(const request_completed_ctx&)>>
        hooks_request_completed_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_RESOURCE_HOOK_TABLE_HPP_
