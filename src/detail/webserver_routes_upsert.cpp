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

// webserver_routes_upsert.cpp -- v2 route-table upsert machinery.
//
// Split out of src/detail/webserver_routes.cpp to keep both translation
// units under the project per-file LOC ceiling (FILE_LOC_MAX in
// scripts/check-file-size.sh). This file holds the detail::webserver_impl
// members that build / merge / insert v2 route-table entries (the
// prepare_or_create_lambda_shim -> commit_handlers_to_shim ->
// upsert_v2_table_entry_locked_ sequence) plus the anonymous-namespace
// for_each_requested_method helper they share. webserver_routes.cpp
// retains the public webserver:: on_*/route/register_ws_resource surface,
// which reaches these members through impl_->...

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
#include "httpserver/detail/route_tier.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

using detail::classify_route_tier;
using detail::route_tier_kind;
using detail::route_tier_result;

// prepare_or_create_lambda_shim implements steps 2-3 of the on_methods_
// upsert sequence; step 1 (input validation) happens in
// webserver_routes.cpp::validate_on_methods_inputs_ before this file's
// sequence runs:
//   2. Look up any existing entry at the path (find_v2_entry_by_path_
//      below). If it's a class-based http_resource, throw -- lambda and
//      class registrations cannot share a path. If it's an existing
//      lambda_resource shim, check that EVERY requested method slot is
//      empty before mutating any of them (atomic all-or-nothing);
//      otherwise throw.
//   3. If no entry exists, build a fresh lambda_resource shim. A later
//      step (upsert_v2_table_entry_locked_ below) inserts it into
//      whichever of the three v2 route-table tiers the endpoint's shape
//      selects: the exact_routes_ hash map, the regex_routes_ vector, or
//      the param_and_prefix_routes_ radix tier.
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

// Returns the route_entry that maps to @p idx in the v2 route table, or
// nullptr if no such entry exists. Probes the three tiers in the same
// order as lookup_v2 (exact -> radix -> regex) but matches on the
// canonical path key (idx.get_url_complete()) rather than a request URL,
// because the on_*/route conflict-detection oracle wants "is there
// already an entry registered AT this path", not "does this request URL
// match a registered route".
//
// Caller must hold route_table_mutex_ (any lock kind). The returned
// pointer is valid only while that lock is held. The pointer is `const`
// because the caller only reads handler/methods to extract the
// lambda_resource shim; the shim itself is mutated through its own
// shared_ptr<>, not through this route_entry.
const detail::route_entry* webserver_impl::find_v2_entry_by_path_(
        const detail::http_endpoint& idx) const noexcept {
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        // Parameterized routes live in the radix tier exclusively.
        // Probe the exact terminus at @p key (is_prefix=false); ignore
        // any prefix terminus -- on_*/route never reach this code with
        // a prefix endpoint (family=false at construction).
        //
        // Note this feeds the radix tier's request-path find() a
        // registered PATTERN string. That works for the common case: a
        // literal "{name}" segment finds no exact child (insert stored
        // it as the wildcard child) and descends that unconstrained
        // wildcard child. A constrained "{id|[0-9]+}" segment, however,
        // fails the constraint regex_match in step_to_child_
        // (radix_tree.hpp) and is NOT found -- the literal pattern text
        // does not satisfy its own constraint.
        detail::radix_match<detail::route_entry> rm;
        if (param_and_prefix_routes_.find(key, rm)
                && rm.entry != nullptr && !rm.is_prefix_match) {
            return rm.entry;
        }
        return nullptr;
    }
    // Non-parameterized: probe exact tier first, then regex tier by
    // url_complete string equality.
    auto it = exact_routes_.find(key);
    if (it != exact_routes_.end()) return &it->second;
    if (idx.is_regex_compiled()) {
        for (const auto& rr : regex_routes_) {
            if (rr.url_complete == key) return &rr.entry;
        }
    }
    return nullptr;
}

// Caller must hold route_table_mutex_ (unique_lock). The shim returned
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
    const detail::route_entry* existing = find_v2_entry_by_path_(idx);
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

void webserver_impl::reject_terminus_collision(const std::string& key,
        bool want_is_prefix) {
    // The route-cache key (method, path) cannot distinguish between an
    // exact_terminus_ and a prefix_terminus_ at the same path, so the
    // tiers must agree on the polarity at each canonical key. Probe
    // BOTH storage locations for an existing entry of the OPPOSITE
    // kind:
    //   - want_is_prefix=true (new prefix): refuse if there's an exact
    //     entry at `key` (either in exact_routes_ for unparameterized
    //     paths, or as a radix exact_terminus_ for parameterized ones).
    //   - want_is_prefix=false (new exact): refuse if there's a prefix
    //     entry at `key` (radix prefix_terminus_ only — there is no
    //     exact-tier storage for prefix routes).
    //
    // Throws BEFORE any mutation so the atomicity guarantee pinned by
    // upsert_param_route_failed_duplicate_leaves_original_intact holds
    // for the new throw too.
    bool collision = false;
    if (!want_is_prefix) {
        // New exact: refuse if a prefix entry already exists at this key.
        collision = param_and_prefix_routes_.has_terminus_at(
            key, /*is_prefix=*/true);
    } else {
        // New prefix: refuse if an exact entry already exists at this key.
        collision = exact_routes_.find(key) != exact_routes_.end()
            || param_and_prefix_routes_.has_terminus_at(
                   key, /*is_prefix=*/false);
    }
    if (collision) {
        const char* incoming_kind = want_is_prefix ? "prefix" : "exact";
        const char* existing_kind = want_is_prefix ? "exact" : "prefix";
        throw std::invalid_argument(
            "Path '" + key + "' is already registered as a "
            + existing_kind
            + " route; cannot also register it as a "
            + incoming_kind
            + " route (the (method, path) cache key cannot "
              "discriminate the two)");
    }
}

void webserver_impl::upsert_v2_radix_route(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // Refuse to plant an exact terminus on a node that
    // already carries a prefix terminus (or vice versa via the
    // symmetric guard in register_v2_route). Must run BEFORE the
    // read-merge below so a thrown exception leaves the table intact.
    reject_terminus_collision(key, /*want_is_prefix=*/false);
    // Read-merge-reinsert: radix_tree::insert always overwrites the
    // terminus, so we must fold any existing entry's methods in first.
    detail::radix_match<detail::route_entry> existing;
    detail::route_entry merged;
    if (param_and_prefix_routes_.find(key, existing)
            && existing.entry && !existing.is_prefix_match) {
        merged = *existing.entry;
    }
    merged.methods = merged.methods | methods;
    merged.handler = std::move(shim);
    merged.is_prefix = false;
    param_and_prefix_routes_.insert(key, std::move(merged), /*is_prefix=*/false);
}

// Construct a non-prefix route_entry. Single helper for the two
// branches of insert_fresh_v2_entry that build the same 3-field shape.
// register_v2_route (webserver_register.cpp) constructs with set_all() +
// caller-controlled is_prefix and stays open-coded; update_existing_v2_entry
// merges into a target rather than constructing fresh.
static detail::route_entry make_non_prefix_entry(
        method_set methods, std::shared_ptr<http_resource> shim) {
    return detail::route_entry{methods, std::move(shim), /*is_prefix=*/false};
}

void webserver_impl::insert_fresh_v2_entry(const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim) {
    auto tier = classify_route_tier(idx);
    switch (tier.kind) {
    case route_tier_kind::radix:
        // Precondition: upsert_v2_table_entry routes url_pars-non-empty paths
        // through upsert_v2_radix_route before calling insert_fresh_v2_entry.
        assert(!"unreachable: radix paths go through upsert_v2_radix_route");
        __builtin_unreachable();
    case route_tier_kind::exact:
        // Refuse to plant an exact entry when a prefix entry
        // for the same canonical path already lives in the radix tier.
        reject_terminus_collision(idx.get_url_complete(),
                                  /*want_is_prefix=*/false);
        exact_routes_.emplace(idx.get_url_complete(),
                              make_non_prefix_entry(methods, std::move(shim)));
        break;
    case route_tier_kind::pattern:
        // Regex-tier routes do not conflict with prefix routes because
        // a literal pattern with regex metacharacters is its own key
        // (it never matches as a prefix lookup target).
        regex_routes_.push_back(
            {idx.get_url_complete(), std::move(*tier.re),
             make_non_prefix_entry(methods, std::move(shim))});
        break;
    }
}

void webserver_impl::update_existing_v2_entry(const std::string& key,
        method_set methods, std::shared_ptr<http_resource> shim) {
    // The tier was fixed at first registration. For the exact tier a
    // direct map lookup suffices; for the regex tier walk the vector
    // and match by shim identity (regex patterns are not repeated
    // keys; pointer identity is the cheapest and most reliable
    // discriminator).
    //
    // Precondition: `shim` is the shared_ptr that
    // prepare_or_create_lambda_shim extracted from this same regex
    // entry (via find_v2_entry_by_path_) earlier in the SAME
    // route_table_mutex_ unique_lock window, so the entry cannot have
    // been removed or its handler replaced in between. The
    // `rr.entry.handler == shim` identity probe therefore cannot miss:
    // the pointer stored in the entry is the very pointer we hold.
    auto merge_into = [&](detail::route_entry& target) {
        target.methods = target.methods | methods;
        target.handler = shim;
        target.is_prefix = false;
    };
    // Radix-tier updates are handled by upsert_v2_radix_route; only exact
    // and regex entries reach here.
    auto exact_it = exact_routes_.find(key);
    if (exact_it != exact_routes_.end()) {
        merge_into(exact_it->second);
        return;
    }
    for (auto& rr : regex_routes_) {
        if (rr.entry.handler == shim) {
            merge_into(rr.entry);
            return;
        }
    }
}

void webserver_impl::upsert_v2_table_entry_locked_(
        const detail::http_endpoint& idx,
        method_set methods, std::shared_ptr<http_resource> shim, bool fresh) {
    // Caller must already hold route_table_mutex_ (unique_lock). The
    // single-lock window covers the conflict probe (carried out by
    // prepare_or_create_lambda_shim via find_v2_entry_by_path_) all the
    // way through the table mutation here, so no concurrent registration
    // can race in between.
    //
    // We store the lambda_resource shim via the shared_ptr arm so
    // dispatch is identical to class-resource registration. The methods
    // bitmask accumulates across calls when fresh==false. `fresh` is
    // prepare_or_create_lambda_shim's returned bool (the call site in
    // on_methods_ names it `is_new_entry`): true iff the shim was newly
    // constructed because no entry existed at this path.
    const std::string& key = idx.get_url_complete();
    if (!idx.get_url_pars().empty()) {
        upsert_v2_radix_route(key, methods, std::move(shim));
    } else if (fresh) {
        insert_fresh_v2_entry(idx, methods, std::move(shim));
    } else {
        update_existing_v2_entry(key, methods, std::move(shim));
    }
}

}  // namespace detail

}  // namespace httpserver
