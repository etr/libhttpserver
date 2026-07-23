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

// Pin canonicalize_lookup_path's observable contract
// behind lookup_v2.  The static helper itself is anonymous-namespaced
// inside webserver_dispatch.cpp and not linkable from tests, so we pin
// its observable effect: a non-canonical spelling of a registered path
// still resolves the same route (found == true, same tier), proving both
// spellings canonicalise to the same key.  Note the exact tier bypasses
// route_lru_cache -- it is a concurrent shared_lock map probe -- so a
// repeated exact-route lookup reports tier_hit::exact, not
// tier_hit::cache.  Also covers the boundary inputs:
//   - empty string -> "/"
//   - "foo" without leading slash -> "/foo"
//   - "/foo/" with trailing slash -> "/foo"
//   - "/" identity
//
// These tests must pass both BEFORE and AFTER the string_view refactor:
// they pin behavior, not internal allocation.  The heap-profile gate in
// step 4 closes the loop on the allocation-elimination side.

#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

namespace {

class noop_resource : public ht::http_resource {
 public:
    ht::http_response render_get(const ht::http_request&) override {
        return ht::http_response::string("ok");
    }
};

ht::detail::webserver_impl& impl_of(ht::webserver& ws) {
    return *ht::webserver_test_access::impl(ws);
}

}  // namespace

LT_BEGIN_SUITE(route_lookup_canonicalize_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(route_lookup_canonicalize_suite)

// ---------------------------------------------------------------------
// Empty input canonicalises to "/" -> the same entry as "/" itself.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(route_lookup_canonicalize_suite, empty_path_maps_to_root)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/", std::make_shared<noop_resource>());

    auto a = impl_of(ws).lookup_v2(ht::http_method::get, std::string(""));
    LT_CHECK(a.found);

    // Second call under the canonical spelling resolves the same exact
    // route the empty-string spelling did, proving both canonicalise to
    // "/". (Exact routes bypass the cache, so this is tier_hit::exact.)
    auto b = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/"));
    LT_CHECK(b.found);
    LT_CHECK(b.tier == ht::detail::webserver_impl::tier_hit::exact);
LT_END_AUTO_TEST(empty_path_maps_to_root)

// ---------------------------------------------------------------------
// Leading-slash insertion: "foo" canonicalises to "/foo".
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(route_lookup_canonicalize_suite,
                   missing_leading_slash_is_prepended)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/foo", std::make_shared<noop_resource>());

    auto a = impl_of(ws).lookup_v2(ht::http_method::get, std::string("foo"));
    LT_CHECK(a.found);
    LT_CHECK(a.entry.methods == ht::method_set{}.set_all());

    // The second lookup under the canonical spelling resolves the same
    // exact route -- proves both spellings canonicalise to the same key.
    // (Exact routes bypass the cache, so this is tier_hit::exact.)
    auto b = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/foo"));
    LT_CHECK(b.found);
    LT_CHECK(b.tier == ht::detail::webserver_impl::tier_hit::exact);
LT_END_AUTO_TEST(missing_leading_slash_is_prepended)

// ---------------------------------------------------------------------
// Trailing-slash stripping: "/foo/" canonicalises to "/foo".  This is
// the case routing_regression_test::exact_path_normalization_aliases
// already covers end-to-end; we pin the cache-sharing property too.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(route_lookup_canonicalize_suite,
                   trailing_slash_is_stripped)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/foo", std::make_shared<noop_resource>());

    auto a = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/foo/"));
    LT_CHECK(a.found);

    // Both spellings canonicalise to "/foo" and resolve the same exact
    // route. (Exact routes bypass the cache, so this is tier_hit::exact.)
    auto b = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/foo"));
    LT_CHECK(b.found);
    LT_CHECK(b.tier == ht::detail::webserver_impl::tier_hit::exact);
LT_END_AUTO_TEST(trailing_slash_is_stripped)

// ---------------------------------------------------------------------
// "/" identity: lookup_v2("/") on a registered "/" route hits the exact
// tier on every call (the exact tier bypasses the cache).  Pins that the
// canonicalisation of "/" does NOT pop its single character.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(route_lookup_canonicalize_suite, root_identity)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/", std::make_shared<noop_resource>());

    auto a = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/"));
    LT_CHECK(a.found);
    LT_CHECK(a.tier == ht::detail::webserver_impl::tier_hit::exact);

    auto b = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/"));
    LT_CHECK(b.found);
    LT_CHECK(b.tier == ht::detail::webserver_impl::tier_hit::exact);
LT_END_AUTO_TEST(root_identity)

// ---------------------------------------------------------------------
// Already-canonical input takes the no-allocation happy path after
// step 1.  Behaviorally observable here: same-shape inputs resolve the
// same exact route on every call.  (The no-allocation property is closed
// by the step 4 heap-profile gate.)
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(route_lookup_canonicalize_suite,
                   already_canonical_is_idempotent)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};
    ws.register_path("/foo", std::make_shared<noop_resource>());

    auto a = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/foo"));
    LT_CHECK(a.found);

    // Exact routes bypass the cache, so this is tier_hit::exact.
    auto b = impl_of(ws).lookup_v2(ht::http_method::get, std::string("/foo"));
    LT_CHECK(b.found);
    LT_CHECK(b.tier == ht::detail::webserver_impl::tier_hit::exact);
LT_END_AUTO_TEST(already_canonical_is_idempotent)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
