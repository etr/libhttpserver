/*
     This file is part of libhttpserver
     Copyright (C) 2021 Alexander Dahl

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

#include <cstdint>
#include <memory>
#include <type_traits>

#include "./httpserver.hpp"
#include "./httpserver/create_test_request.hpp"
#include "./littletest.hpp"

using std::shared_ptr;

using httpserver::create_test_request;
using httpserver::http_method;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::method_set;

class simple_resource : public http_resource {
 public:
     http_response render_get(const http_request&) override {
         return http_response::string("OK");
     }
};

// http_resource should be smaller than a map-based v1 resource
// (vptr + uint32_t + padding). Empty std::map is typically ~48 bytes
// on libstdc++/libc++; the v1 resource was ~56-64 bytes.
//
// TASK-058 step 3 grew the layout by the lazy Allow-header cache
// payload (std::mutex + std::string + method_set + bool); the
// authoritative envelope check lives in
// test/bench_sizeof_http_resource.cpp, which anchors against the v1
// baseline.  The ceiling here is bumped to keep the file building --
// the bench's v1-anchored check is the production-gate value, not
// this one.  See bench_sizeof_http_resource.cpp for the full algebra.
static_assert(sizeof(http_resource) <= 256,
              "http_resource grew beyond the post-TASK-058-step-3 layout "
              "envelope (Allow-header cache: mutex + string + method_set "
              "+ bool); see bench_sizeof_http_resource.cpp for the "
              "authoritative v1-anchored static_assert");

// TASK-036 acceptance: render_* virtuals return http_response by value.
// Pins PRD-RSP-REQ-007 / DR-004 / DR-010 at compile time so any future
// regression to shared_ptr<http_response> fails to compile rather than
// silently restoring the v1 dispatch shape.
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_get(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_get must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_post(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_post must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_put(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_put must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_head(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_head must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_delete(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_delete must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_trace(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_trace must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_options(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_options must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_patch(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_patch must return http_response by value (TASK-036)");
static_assert(std::is_same_v<
                  decltype(std::declval<http_resource&>().render_connect(
                      std::declval<const http_request&>())),
                  http_response>,
              "http_resource::render_connect must return http_response by value (TASK-036)");

// Const-noexcept acceptance criteria pinned at compile time: both
// query members must be callable on a const reference and must be
// declared noexcept.
static_assert(noexcept(std::declval<const http_resource&>()
                           .is_allowed(http_method::get)),
              "is_allowed(http_method) must be const noexcept");
static_assert(noexcept(std::declval<const http_resource&>()
                           .get_allowed_methods()),
              "get_allowed_methods() must be const noexcept");

LT_BEGIN_SUITE(http_resource_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_resource_suite)

LT_BEGIN_AUTO_TEST(http_resource_suite, disallow_all_methods)
    simple_resource sr;
    sr.disallow_all();
    method_set allowed = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed.bits, 0u);
LT_END_AUTO_TEST(disallow_all_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, allow_some_methods)
    simple_resource sr;
    sr.disallow_all();
    sr.set_allowing(http_method::get, true);
    sr.set_allowing(http_method::post, true);
    method_set allowed = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed.contains(http_method::get), true);
    LT_CHECK_EQ(allowed.contains(http_method::post), true);
    LT_CHECK_EQ(allowed.contains(http_method::put), false);
    LT_CHECK_EQ(allowed.bits,
                method_set{}.set(http_method::get).set(http_method::post).bits);
LT_END_AUTO_TEST(allow_some_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, allow_all_methods)
    simple_resource sr;
    sr.allow_all();
    method_set allowed = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed.contains(http_method::get), true);
    LT_CHECK_EQ(allowed.contains(http_method::head), true);
    LT_CHECK_EQ(allowed.contains(http_method::post), true);
    LT_CHECK_EQ(allowed.contains(http_method::put), true);
    LT_CHECK_EQ(allowed.contains(http_method::del), true);
    LT_CHECK_EQ(allowed.contains(http_method::connect), true);
    LT_CHECK_EQ(allowed.contains(http_method::options), true);
    LT_CHECK_EQ(allowed.contains(http_method::trace), true);
    LT_CHECK_EQ(allowed.contains(http_method::patch), true);
LT_END_AUTO_TEST(allow_all_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_disable)
    simple_resource sr;
    // By default, GET is allowed.
    LT_CHECK_EQ(sr.is_allowed(http_method::get), true);
    sr.set_allowing(http_method::get, false);
    LT_CHECK_EQ(sr.is_allowed(http_method::get), false);
    sr.set_allowing(http_method::get, true);
    LT_CHECK_EQ(sr.is_allowed(http_method::get), true);
LT_END_AUTO_TEST(set_allowing_disable)

// Test resource that only overrides render() method
class render_only_resource : public http_resource {
 public:
    http_response render(const http_request&) override {
        return http_response::string("render called");
    }
};

// Test resource with no overrides at all
class empty_resource : public http_resource {
 public:
    // No render methods overridden - uses defaults
};

LT_BEGIN_AUTO_TEST(http_resource_suite, default_render_returns_sentinel)
    // The unique contract of TASK-036: the default render() returns a
    // default-constructed http_response whose status_code_ == -1 is the
    // v1-compatible "handler did not produce a response" sentinel.
    // finalize_answer recognises -1 and routes through internal_error_page.
    // This is the one contract that empty_resource exercises that
    // allow_all_methods / is_allowed_known_methods do NOT.
    //
    // TASK-058 housekeeping: http_request() is private (only the
    // libhttpserver translation units can default-construct one);
    // public consumers (including tests) construct an http_request
    // through create_test_request().build().  The previous bare
    // `http_request req;` form was a build break at baseline; this
    // is the same observable contract reachable from the supported
    // construction surface.
    empty_resource er;
    http_request req = create_test_request().build();
    http_response resp = er.render(req);
    LT_CHECK_EQ(resp.get_status(), -1);
    // render_get / render_post / etc. forward to render(), so they also
    // return the -1 sentinel by default.
    http_response resp_get = er.render_get(req);
    LT_CHECK_EQ(resp_get.get_status(), -1);
LT_END_AUTO_TEST(default_render_returns_sentinel)

LT_BEGIN_AUTO_TEST(http_resource_suite, render_only_resource_methods_allowed)
    render_only_resource ror;
    // All methods should be allowed by default
    LT_CHECK_EQ(ror.is_allowed(http_method::get), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::post), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::put), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::head), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::del), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::trace), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::connect), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::options), true);
    LT_CHECK_EQ(ror.is_allowed(http_method::patch), true);
LT_END_AUTO_TEST(render_only_resource_methods_allowed)

LT_BEGIN_AUTO_TEST(http_resource_suite, resource_init_sets_all_methods)
    simple_resource sr;
    // The default-constructed resource has every valid method bit set.
    method_set allowed = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed.bits, method_set{}.set_all().bits);
LT_END_AUTO_TEST(resource_init_sets_all_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, get_allowed_methods_only_returns_set)
    simple_resource sr;
    sr.set_allowing(http_method::post, false);
    sr.set_allowing(http_method::put, false);
    sr.set_allowing(http_method::del, false);

    method_set allowed = sr.get_allowed_methods();
    LT_CHECK_EQ(allowed.contains(http_method::post), false);
    LT_CHECK_EQ(allowed.contains(http_method::put), false);
    LT_CHECK_EQ(allowed.contains(http_method::del), false);
    // Other methods stay allowed.
    LT_CHECK_EQ(allowed.contains(http_method::get), true);
    LT_CHECK_EQ(allowed.contains(http_method::head), true);
    LT_CHECK_EQ(allowed.contains(http_method::trace), true);
LT_END_AUTO_TEST(get_allowed_methods_only_returns_set)

LT_BEGIN_AUTO_TEST(http_resource_suite, is_allowed_known_methods)
    simple_resource sr;
    LT_CHECK_EQ(sr.is_allowed(http_method::get), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::post), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::put), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::head), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::del), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::trace), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::connect), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::options), true);
    LT_CHECK_EQ(sr.is_allowed(http_method::patch), true);
LT_END_AUTO_TEST(is_allowed_known_methods)

LT_BEGIN_AUTO_TEST(http_resource_suite, allow_all_after_disallow_all)
    simple_resource sr;
    sr.disallow_all();
    LT_CHECK_EQ(sr.get_allowed_methods().bits, 0u);

    sr.allow_all();
    LT_CHECK_EQ(sr.get_allowed_methods().bits, method_set{}.set_all().bits);
LT_END_AUTO_TEST(allow_all_after_disallow_all)

LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_multiple_times)
    simple_resource sr;
    LT_CHECK_EQ(sr.is_allowed(http_method::get), true);
    sr.set_allowing(http_method::get, false);
    LT_CHECK_EQ(sr.is_allowed(http_method::get), false);
    sr.set_allowing(http_method::get, true);
    LT_CHECK_EQ(sr.is_allowed(http_method::get), true);
    sr.set_allowing(http_method::get, false);
    sr.set_allowing(http_method::get, false);  // Double false
    LT_CHECK_EQ(sr.is_allowed(http_method::get), false);
LT_END_AUTO_TEST(set_allowing_multiple_times)

// security: set_allowing(count_, true) must have no effect — the sentinel
// must never appear in the allowed-set so is_allowed(count_) stays false.
LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_count_sentinel_has_no_effect)
    simple_resource sr;
    sr.set_allowing(http_method::count_, true);
    // The sentinel bit must not have been set.
    LT_CHECK_EQ(sr.is_allowed(http_method::count_), false);
    // The rest of the allowed set must be unchanged (still all-set by default).
    LT_CHECK_EQ(sr.get_allowed_methods().bits, method_set{}.set_all().bits);
LT_END_AUTO_TEST(set_allowing_count_sentinel_has_no_effect)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
