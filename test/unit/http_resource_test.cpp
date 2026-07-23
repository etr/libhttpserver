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

// sizeof(http_resource) tripwire.  This is a bystander
// gate: any new field on http_resource breaks the build until the
// maintainer rolls the threshold and records the new size in the
// table below.  The authoritative v1-anchored static_assert lives in
// test/bench_sizeof_http_resource.cpp; this one is the day-to-day
// tripwire that runs as part of `make check`.
//
// Per-lane observed sizes (locked at the value of
// max(observed) across every CI lane that compiles this TU; CI lanes
// are enumerated in .github/workflows/verify-build.yml):
//
//   | CI lane                                      | observed bytes |
//   |----------------------------------------------|----------------|
//   | macos-latest / Apple clang 21 / libc++       |            232 |  <- dominates: std::shared_mutex ~168B on libc++ (directly measured)
//   | ubuntu-latest / gcc 11..14 / libstdc++       |           ~104 |  std::shared_mutex ~56B on libstdc++ (inferred from libstdc++ ABI; not directly measured on this lane)
//   | ubuntu-latest / clang 13..18 / libstdc++     |           ~104 |  same ABI as above (inferred, not directly measured on this lane)
//   | windows-latest / MINGW64 gcc / libstdc++     |           ~104 |  inferred from libstdc++ ABI (shared_mutex/string sizes); not directly measured on this lane
//   | windows-latest / MSYS gcc / libstdc++        |           ~104 |  inferred from libstdc++ ABI (shared_mutex/string sizes); not directly measured on this lane
//
// Layout: vptr (8) + methods_allowed_ (4) + pad (4) +
//   shared_ptr<resource_hook_table> hook_table_ (16) +
//   std::shared_mutex cached_allow_mutex_ (stdlib-dependent) +
//   std::string cached_allow_header_ (24 libc++ / 32 libstdc++) +
//   method_set cached_allow_mask_ (4) + bool cached_allow_valid_ (1) +
//   padding to next 8-byte boundary.
//
// Slack: +16 bytes (one cache-line-aligned to size_t).  Tight enough
// that a new pointer-sized member trips this; loose enough to absorb
// one alignment-pad shift across an ABI bump.  When this fires, the
// maintainer must:
//   1) re-measure on every lane (use a probe `static_assert(sizeof(...)
//      == 0, ...)` line which forces the compiler to print the integer
//      operand in the failure message, push to CI, read the numbers
//      from each lane's compile log, then revert the probe);
//   2) bump the threshold to max(observed) + 16;
//   3) update the table above.
//
// Threshold = max(observed) + 16 = 232 + 16 = 248.
static_assert(sizeof(http_resource) <= 248,
              "http_resource size grew beyond the recorded per-lane "
              "max + 16-byte slack; see comment table above for the "
              "re-measurement procedure");

// render_* virtuals return http_response by value.
// Pinned at compile time so any future
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
    // The littletest framework invokes set_up() and tear_down() via CRTP
    // on the suite base; they must be defined even when empty.
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
    // The unique contract here: the default render() returns a
    // default-constructed http_response whose status_code_ == -1 is the
    // v1-compatible "handler did not produce a response" sentinel.
    // finalize_answer recognises -1 and routes through internal_error_page.
    // This is the one contract that empty_resource exercises that
    // allow_all_methods / is_allowed_known_methods do NOT.
    //
    // Note: http_request() is private (only the
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
LT_END_AUTO_TEST(default_render_returns_sentinel)

// render_get / render_post / render_delete / etc. all forward to render(),
// so they also return the -1 sentinel by default. Split from
// default_render_returns_sentinel above (and extended to more than one
// verb) so a failure localises to the specific forwarding path that broke
// rather than an ambiguous single test body.
LT_BEGIN_AUTO_TEST(http_resource_suite, default_render_get_returns_sentinel)
    empty_resource er;
    http_request req = create_test_request().build();
    http_response resp_get = er.render_get(req);
    LT_CHECK_EQ(resp_get.get_status(), -1);
LT_END_AUTO_TEST(default_render_get_returns_sentinel)

LT_BEGIN_AUTO_TEST(http_resource_suite, default_render_post_returns_sentinel)
    empty_resource er;
    http_request req = create_test_request().build();
    http_response resp_post = er.render_post(req);
    LT_CHECK_EQ(resp_post.get_status(), -1);
    http_response resp_delete = er.render_delete(req);
    LT_CHECK_EQ(resp_delete.get_status(), -1);
LT_END_AUTO_TEST(default_render_post_returns_sentinel)

// render_only_resource's default-allowed-method state is already covered
// by is_allowed_known_methods (canonical for that contract) and by
// resource_init_sets_all_methods / allow_all_methods. This test's unique
// purpose is verifying that render_only_resource's render() override is
// actually invoked through dispatch (render_get() forwards to render()
// when only render() is overridden); it previously re-checked the
// already-covered allowed-method state instead.
// render() constructs its body via http_response::string(), whose status
// defaults to 200 (vs. the base http_resource::render()'s -1 sentinel --
// see default_render_returns_sentinel above), so a non-sentinel status
// here pins that the override actually ran.
LT_BEGIN_AUTO_TEST(http_resource_suite, render_only_resource_dispatches_through_render)
    render_only_resource ror;
    http_request req = create_test_request().build();
    LT_CHECK_EQ(ror.render(req).get_status(), 200);
    LT_CHECK_EQ(ror.render_get(req).get_status(), 200);
LT_END_AUTO_TEST(render_only_resource_dispatches_through_render)

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

LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_toggle_round_trip)
    simple_resource sr;
    LT_CHECK_EQ(sr.is_allowed(http_method::get), true);
    sr.set_allowing(http_method::get, false);
    LT_CHECK_EQ(sr.is_allowed(http_method::get), false);
    sr.set_allowing(http_method::get, true);
    LT_CHECK_EQ(sr.is_allowed(http_method::get), true);
LT_END_AUTO_TEST(set_allowing_toggle_round_trip)

LT_BEGIN_AUTO_TEST(http_resource_suite, set_allowing_double_false_idempotent)
    simple_resource sr;
    sr.set_allowing(http_method::get, false);
    sr.set_allowing(http_method::get, false);  // Double false
    LT_CHECK_EQ(sr.is_allowed(http_method::get), false);
LT_END_AUTO_TEST(set_allowing_double_false_idempotent)

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
