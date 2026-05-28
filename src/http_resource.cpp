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

// TASK-051 -- Out-of-line bodies for http_resource::add_hook overloads
// + the lazy hook_table_ allocator. The public header (http_resource.hpp)
// stays free of <vector>/<atomic>/<shared_mutex> transitives: only the
// shared_ptr<detail::resource_hook_table> PIMPL slot is visible there.

#include "httpserver/http_resource.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "httpserver/detail/resource_hook_table.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"

namespace httpserver {

namespace {

[[noreturn]] void throw_wrong_phase(hook_phase requested,
                                    hook_phase expected) {
    throw std::invalid_argument(
        std::string("http_resource::add_hook: invalid phase ") +
        std::string(to_string(requested)) +
        " -- per-route hooks accept only " +
        std::string(to_string(expected)) +
        " on this overload (and one of: before_handler, "
        "handler_exception, after_handler, response_sent, "
        "request_completed -- the post-route-resolution phases). "
        "Earlier phases (connection_opened, accept_decision, "
        "request_received, body_chunk, route_resolved, "
        "connection_closed) are server-scope only because the resource "
        "is not yet known when they fire.");
}

void check_phase(hook_phase requested, hook_phase expected) {
    if (requested != expected) {
        throw_wrong_phase(requested, expected);
    }
}

void check_fn(bool empty) {
    if (empty) {
        throw std::invalid_argument(
            "http_resource::add_hook: hook callable must not be empty");
    }
}

// Lazy CAS-style allocator. Resources that never register a hook keep
// hook_table_ at nullptr and pay zero allocation cost. Concurrent first-
// callers each construct a fresh table; the CAS winner installs theirs,
// the loser discards its local. At most one allocation is wasted under
// contention (acceptable -- registration is rare).
std::shared_ptr<detail::resource_hook_table>
ensure_table(std::shared_ptr<detail::resource_hook_table>& slot) {
    auto existing = std::atomic_load_explicit(
        &slot, std::memory_order_acquire);
    if (existing) {
        return existing;
    }
    auto fresh = std::make_shared<detail::resource_hook_table>();
    std::shared_ptr<detail::resource_hook_table> expected;
    if (std::atomic_compare_exchange_strong_explicit(
            &slot, &expected, fresh,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return fresh;
    }
    // Lost the race; `expected` was updated to the winning shared_ptr.
    return expected;
}

}  // namespace

// ---- add_hook overloads -------------------------------------------------

hook_handle http_resource::add_hook(hook_phase phase,
        std::function<hook_action(before_handler_ctx&)> fn) {
    check_phase(phase, hook_phase::before_handler);
    check_fn(!fn);
    auto table = ensure_table(hook_table_);
    const std::uint64_t id = table->append_before_handler(std::move(fn));
    return hook_handle{std::weak_ptr<detail::resource_hook_table>(table),
                       hook_phase::before_handler, id};
}

hook_handle http_resource::add_hook(hook_phase phase,
        std::function<hook_action(const handler_exception_ctx&)> fn) {
    check_phase(phase, hook_phase::handler_exception);
    check_fn(!fn);
    auto table = ensure_table(hook_table_);
    const std::uint64_t id = table->append_handler_exception(std::move(fn));
    return hook_handle{std::weak_ptr<detail::resource_hook_table>(table),
                       hook_phase::handler_exception, id};
}

hook_handle http_resource::add_hook(hook_phase phase,
        std::function<hook_action(after_handler_ctx&)> fn) {
    check_phase(phase, hook_phase::after_handler);
    check_fn(!fn);
    auto table = ensure_table(hook_table_);
    const std::uint64_t id = table->append_after_handler(std::move(fn));
    return hook_handle{std::weak_ptr<detail::resource_hook_table>(table),
                       hook_phase::after_handler, id};
}

hook_handle http_resource::add_hook(hook_phase phase,
        std::function<void(const response_sent_ctx&)> fn) {
    check_phase(phase, hook_phase::response_sent);
    check_fn(!fn);
    auto table = ensure_table(hook_table_);
    const std::uint64_t id = table->append_response_sent(std::move(fn));
    return hook_handle{std::weak_ptr<detail::resource_hook_table>(table),
                       hook_phase::response_sent, id};
}

hook_handle http_resource::add_hook(hook_phase phase,
        std::function<void(const request_completed_ctx&)> fn) {
    check_phase(phase, hook_phase::request_completed);
    check_fn(!fn);
    auto table = ensure_table(hook_table_);
    const std::uint64_t id = table->append_request_completed(std::move(fn));
    return hook_handle{std::weak_ptr<detail::resource_hook_table>(table),
                       hook_phase::request_completed, id};
}

}  // namespace httpserver
