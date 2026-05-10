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

// TASK-027: tier-order pin for the v2 route lookup pipeline. Drives the
// public webserver registration surface (register_path / on_get) and
// then peeks into the impl via webserver_impl::lookup_v2() to assert
//   1. an exact-registered path hits the exact tier;
//   2. a parameterized path hits the radix tier and captures the
//      parameter;
//   3. a prefix path hits the radix tier with is_prefix_match;
//   4. a second lookup of the same path hits the cache.

#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

class noop_resource : public ht::http_resource {
 public:
    std::shared_ptr<ht::http_response> render_get(const ht::http_request&) override {
        return std::make_shared<ht::http_response>(ht::http_response::string("ok"));
    }
};

// Construct a minimal webserver, register a few routes through the public
// surface, then probe the impl directly. We never start the daemon, so the
// MHD layer is not invoked and the test exercises only the route-table
// data path.

LT_BEGIN_SUITE(lookup_pipeline_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(lookup_pipeline_suite)

LT_BEGIN_AUTO_TEST(lookup_pipeline_suite, exact_path_hits_exact_tier_and_promotes_to_cache)
    ht::webserver ws = ht::create_webserver(8080).start_method(ht::http::http_utils::INTERNAL_SELECT);
    ws.register_path("/exact", std::make_shared<noop_resource>());

    auto& impl = *ht::webserver_test_access::impl(ws);
    auto r1 = impl.lookup_v2(ht::http_method::get, std::string("/exact"));
    LT_CHECK(r1.found);
    LT_CHECK(r1.tier == ht::detail::webserver_impl::tier_hit::exact);

    // Second lookup: cache hit.
    auto r2 = impl.lookup_v2(ht::http_method::get, std::string("/exact"));
    LT_CHECK(r2.found);
    LT_CHECK(r2.tier == ht::detail::webserver_impl::tier_hit::cache);
LT_END_AUTO_TEST(exact_path_hits_exact_tier_and_promotes_to_cache)

LT_BEGIN_AUTO_TEST(lookup_pipeline_suite, parameterized_path_hits_radix_tier_and_captures)
    ht::webserver ws = ht::create_webserver(8080).start_method(ht::http::http_utils::INTERNAL_SELECT);
    ws.register_path("/users/{id}/posts", std::make_shared<noop_resource>());

    auto& impl = *ht::webserver_test_access::impl(ws);
    auto r = impl.lookup_v2(ht::http_method::get, std::string("/users/42/posts"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::radix);
    LT_CHECK_EQ(r.captured_params.size(), static_cast<std::size_t>(1));
    LT_CHECK_EQ(r.captured_params[0].first, std::string("id"));
    LT_CHECK_EQ(r.captured_params[0].second, std::string("42"));
LT_END_AUTO_TEST(parameterized_path_hits_radix_tier_and_captures)

LT_BEGIN_AUTO_TEST(lookup_pipeline_suite, prefix_path_hits_radix_tier_and_serves_subpaths)
    ht::webserver ws = ht::create_webserver(8080).start_method(ht::http::http_utils::INTERNAL_SELECT);
    ws.register_prefix("/static", std::make_shared<noop_resource>());

    auto& impl = *ht::webserver_test_access::impl(ws);
    auto r1 = impl.lookup_v2(ht::http_method::get, std::string("/static"));
    LT_CHECK(r1.found);
    LT_CHECK(r1.tier == ht::detail::webserver_impl::tier_hit::radix);

    // /static was looked up above, but /static/foo/bar has not been looked up
    // yet. On this first lookup, it must come from the radix tier (cache miss).
    auto r2 = impl.lookup_v2(ht::http_method::get, std::string("/static/foo/bar"));
    LT_CHECK(r2.found);
    LT_CHECK(r2.tier == ht::detail::webserver_impl::tier_hit::radix);
LT_END_AUTO_TEST(prefix_path_hits_radix_tier_and_serves_subpaths)

LT_BEGIN_AUTO_TEST(lookup_pipeline_suite, miss_returns_tier_hit_none)
    ht::webserver ws = ht::create_webserver(8080).start_method(ht::http::http_utils::INTERNAL_SELECT);
    ws.register_path("/exact", std::make_shared<noop_resource>());

    auto& impl = *ht::webserver_test_access::impl(ws);
    auto r = impl.lookup_v2(ht::http_method::get, std::string("/nope"));
    LT_CHECK(!r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::none);
LT_END_AUTO_TEST(miss_returns_tier_hit_none)

// Test that the prefix_path test assertion is tight: /static/foo/bar must
// hit radix (cache miss on first lookup, never looked up before).
LT_BEGIN_AUTO_TEST(lookup_pipeline_suite, prefix_subpath_first_lookup_hits_radix_not_cache)
    ht::webserver ws = ht::create_webserver(8080).start_method(ht::http::http_utils::INTERNAL_SELECT);
    ws.register_prefix("/static", std::make_shared<noop_resource>());

    auto& impl = *ht::webserver_test_access::impl(ws);
    // Do NOT look up /static first — /static/foo/bar has never been cached.
    auto r = impl.lookup_v2(ht::http_method::get, std::string("/static/foo/bar"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::radix);
LT_END_AUTO_TEST(prefix_subpath_first_lookup_hits_radix_not_cache)

// Test that a "pure regex" route (regex_checking enabled, no {name} params,
// regex special chars in the path) is served by the regex tier.
LT_BEGIN_AUTO_TEST(lookup_pipeline_suite, regex_route_hits_regex_tier)
    // regex_checking is enabled by default in create_webserver.
    ht::webserver ws = ht::create_webserver(8080).start_method(ht::http::http_utils::INTERNAL_SELECT);
    // /api/v[0-9]+ has no {name} params but contains regex characters.
    // With regex_checking=true (default) this is compiled as a regex route.
    ws.register_path("/api/v[0-9]+", std::make_shared<noop_resource>());

    auto& impl = *ht::webserver_test_access::impl(ws);
    // /api/v1 should be matched by the regex pattern.
    auto r = impl.lookup_v2(ht::http_method::get, std::string("/api/v1"));
    LT_CHECK(r.found);
    LT_CHECK(r.tier == ht::detail::webserver_impl::tier_hit::regex);
LT_END_AUTO_TEST(regex_route_hits_regex_tier)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
