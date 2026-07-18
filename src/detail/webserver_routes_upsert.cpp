/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

// webserver_routes_upsert.cpp -- on_*/route lambda-shim registration
// POLICY.
//
// Split out of src/detail/webserver_routes.cpp to keep both translation
// units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). This file holds the detail::webserver_impl
// members that own the lambda_resource shim lifecycle for on_*/route
// registration (prepare_or_create_lambda_shim -> commit_handlers_to_shim)
// plus the anonymous-namespace for_each_requested_method helper they
// share. The v2 route-table conflict probe + table mutation
// (find_v2_entry_by_path_ / upsert_v2_table_entry_locked_ and the reject_*
// guards) live in the route_table collaborator (src/detail/route_table.cpp);
// this file reaches them through impl's routes_ member under a
// routes_.lock_for_write() window. webserver_routes.cpp retains the public
// webserver:: on_*/route/register_ws_resource surface, which reaches these
// members through impl_->...

#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "httpserver/http_method.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/detail/http_endpoint.hpp"
#include "httpserver/detail/lambda_resource.hpp"
#include "httpserver/detail/route_entry.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

// prepare_or_create_lambda_shim implements steps 2-3 of the on_methods_
// upsert sequence; step 1 (input validation) happens in
// webserver_routes.cpp::validate_on_methods_inputs_ before this file's
// sequence runs:
//   2. Look up any existing entry at the path
//      (routes_.find_v2_entry_by_path_). If it's a class-based
//      http_resource, throw -- lambda and class registrations cannot
//      share a path. If it's an existing lambda_resource shim, check that
//      EVERY requested method slot is empty before mutating any of them
//      (atomic all-or-nothing); otherwise throw.
//   3. If no entry exists, build a fresh lambda_resource shim. A later
//      step (routes_.upsert_v2_table_entry_locked_, in route_table.cpp)
//      inserts it into whichever of the three v2 route-table tiers the
//      endpoint's shape selects: the exact_routes_ hash map, the
//      regex_routes_ vector, or the param_and_prefix_routes_ radix tier.
//   4. Write @p handler into each requested method slot
//      (commit_handlers_to_shim below).
//   5. Invalidate the LRU route cache -- done by the caller
//      (webserver_routes.cpp::on_methods_) after the table lock is
//      released, not in this file.
//
// The dispatch path in finalize_answer is not modified: it already
// looks up via shared_ptr<http_resource>, calls is_allowed(method)
// gating the 405 path, then dispatches via the per-method member-
// function pointer set in answer_to_connection. The lambda_resource
// shim's render_* overrides invoke the stored slot.
namespace {

// Iterate enum-declaration order (get, head, post, ...) over the bits
// set in @p methods, invoking @p fn for each. Used by the on_methods_
// pre-check loop and the commit loop; pulling the scaffolding into a
// single helper dedupes the iteration boilerplate. The order matches
// http_method enum-declaration order, which is also the serialization
// order for the `Allow:` header.
template <typename Fn>
void for_each_requested_method(method_set methods, Fn&& fn) {
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (methods.contains(m)) fn(m);
    }
}

}  // namespace

namespace detail {

// Caller must hold routes_.lock_for_write() (unique_lock). The shim returned
// here is the SAME object that subsequent helpers (commit_handlers_to_shim,
// upsert_v2_table_entry_locked_) mutate; holding the lock across the
// whole prepare->commit->upsert sequence prevents a concurrent registration
// from racing in between.
//
// The returned bool (written /*fresh=*/ below) is true iff the shim was
// newly constructed because no entry existed at this path. on_methods_
// receives it as `is_new_entry` and forwards it unchanged as the `fresh`
// parameter of upsert_v2_table_entry_locked_ -- all three names denote
// the same flag.
std::pair<std::shared_ptr<detail::lambda_resource>, bool>
webserver_impl::prepare_or_create_lambda_shim(const detail::http_endpoint& idx,
                                              method_set methods) {
    const detail::route_entry* existing = routes_.find_v2_entry_by_path_(idx);
    if (existing == nullptr) {
        return {std::make_shared<detail::lambda_resource>(), /*fresh=*/true};
    }
    // Existing entry. route_entry::handler is a
    // shared_ptr<http_resource>. Dynamic-cast to
    // lambda_resource: if the cast misses, a class-based
    // register_path/register_prefix owns this path and we must throw.
    auto shim = std::dynamic_pointer_cast<detail::lambda_resource>(
        existing->handler);
    if (!shim) {
        throw std::invalid_argument(
            "A non-lambda http_resource is already registered at "
            "this path; on_*/route cannot share a path with "
            "register_path/register_prefix");
    }
    // Atomicity pre-check: every requested slot must be empty BEFORE
    // we mutate any of them.
    for_each_requested_method(methods, [&](http_method m) {
        if (shim->has_slot(m)) {
            throw std::invalid_argument(
                "A handler is already registered for one of the "
                "requested methods on this path");
        }
    });
    return {shim, /*fresh=*/false};
}

void webserver_impl::commit_handlers_to_shim(detail::lambda_resource& shim,
        method_set methods,
        std::function<::httpserver::http_response(
            const ::httpserver::http_request&)> handler) {
    // Collect the set of requested methods in iteration order so the
    // loop below can identify the last one. Using a small inline array
    // avoids a heap allocation in the common case (N <= 9 methods).
    //
    // Move-into-last-slot optimisation:
    // all but the last slot receive a copy; the last slot is populated
    // by moving the handler, avoiding one extra heap allocation when the
    // std::function's capture is too large for the SBO buffer.
    std::array<http_method, static_cast<std::size_t>(http_method::count_)> buf{};
    std::size_t count = 0;
    for_each_requested_method(methods, [&](http_method m) {
        buf[count++] = m;
    });
    for (std::size_t i = 0; i < count; ++i) {
        if (i + 1 < count) {
            shim.set_slot(buf[i], handler);          // copy
        } else {
            shim.set_slot(buf[i], std::move(handler));  // move into last slot
        }
    }
}

}  // namespace detail

}  // namespace httpserver
