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

#include "httpserver/detail/method_utils.hpp"
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

// ---- copy / move special members ----------------------------------------
//
// TASK-058 step 3 added std::mutex cached_allow_mutex_, which has no
// copy or move.  The defaulted special members on the public class
// declaration would therefore be implicitly deleted.  Implement them
// here by-hand, skipping the mutex.  The copy / move target starts
// with a fresh, default-constructed mutex and an invalidated Allow-
// header cache (cached_allow_valid_ = false), which the next call to
// get_allow_header() repopulates lazily.

http_resource::http_resource(const http_resource& b) noexcept
    : methods_allowed_(b.methods_allowed_),
      hook_table_(b.hook_table_) {
    // cached_allow_mutex_ default-constructs.
    // cached_allow_header_ / cached_allow_mask_ default-construct.
    // cached_allow_valid_ stays false (member default).
}

http_resource::http_resource(http_resource&& b) noexcept
    : methods_allowed_(b.methods_allowed_),
      hook_table_(std::move(b.hook_table_)) {
    // Same rationale as the copy constructor: cache state is per-
    // instance and is not transferred.  std::mutex has no move.
}

http_resource& http_resource::operator=(const http_resource& b) noexcept {
    if (this != &b) {
        hook_table_ = b.hook_table_;
        methods_allowed_ = b.methods_allowed_;
        // Invalidate the local cache; do NOT touch the mutex (still
        // owned by *this).
        std::lock_guard<std::mutex> lock(cached_allow_mutex_);
        cached_allow_valid_ = false;
        cached_allow_header_.clear();
    }
    return *this;
}

http_resource& http_resource::operator=(http_resource&& b) noexcept {
    if (this != &b) {
        hook_table_ = std::move(b.hook_table_);
        methods_allowed_ = b.methods_allowed_;
        std::lock_guard<std::mutex> lock(cached_allow_mutex_);
        cached_allow_valid_ = false;
        cached_allow_header_.clear();
    }
    return *this;
}

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

// ---- get_allow_header ---------------------------------------------------
//
// TASK-058 step 3: lazy Allow-header cache.  See the header-side
// declaration for the contract.  Implementation:
//
//   1. Snapshot the live mask once -- subsequent comparisons run against
//      this local copy (avoids re-loading methods_allowed_ inside the
//      critical section).
//   2. Take the per-resource mutex.  Under contention this serialises
//      cache-fill; under the warm-path (hit) case the lock is held for
//      a single integer compare + reference return.
//   3. If the cached snapshot matches the live mask, return the cached
//      string by reference.  Otherwise rebuild via detail::format_allow_header
//      (the same routine the pre-TASK-058 path used) and update the snapshot.
//
// The returned reference is stable until the next mask mutation; the
// caller (dispatch_resource_handler) consumes it synchronously within
// the current dispatch, so the reference outlives use.
const std::string& http_resource::get_allow_header() const {
    const method_set live = methods_allowed_;
    std::lock_guard<std::mutex> lock(cached_allow_mutex_);
    if (!cached_allow_valid_ || cached_allow_mask_ != live) {
        cached_allow_header_ = detail::format_allow_header(live);
        cached_allow_mask_ = live;
        cached_allow_valid_ = true;
    }
    return cached_allow_header_;
}

}  // namespace httpserver
