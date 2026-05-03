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

// TASK-010 unit test: static factory functions on http_response.
//
// Each factory placement-news the corresponding detail::body subclass
// into the SBO buffer (or, in the future, onto the heap) and tags the
// response with the appropriate body_kind. Tests cover:
//   * the public observable contract: kind(), get_response_code(),
//     get_header() — the surface a v2 caller sees;
//   * the SBO inline placement, asserted through the existing
//     http_response_sbo_test_access friend so no new private members
//     are exposed;
//   * the lifetime guarantees called out by AC #4 (pipe fd ownership)
//     and AC #3 (unauthorized status + header).
//
// The TU is built with -DHTTPSERVER_COMPILATION (set by the test
// AM_CPPFLAGS) so it can include httpserver/detail/body.hpp directly,
// matching http_response_sbo_test.cpp's pattern.
//
// Header hygiene note: this TU does NOT include <sys/uio.h>. AC #2
// requires that http_response::iovec(...) compile from user code
// without that header in scope; the umbrella header_hygiene tests
// guard the umbrella surface, and this file simply does not pull it
// in to give callers a working reference.

#include <microhttpd.h>
#include <unistd.h>          // pipe, close (POSIX)
#include <fcntl.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"                 // public umbrella
#include "httpserver/detail/body.hpp"       // private detail::body (test-only)
#include "./littletest.hpp"

using httpserver::body_kind;
using httpserver::http_response;

// http_response_sbo_test_access is the same friend struct used by
// http_response_sbo_test.cpp. Since we only need read access to the
// SBO inline flag and body kind here, we accept the cross-TU duplicate
// symbol rule by declaring (NOT defining) the struct here as a
// friend-only forward declaration is impossible. Instead, we re-define
// the struct in this TU's anonymous namespace via the friend hook
// already declared in http_response.hpp. Defining a non-anonymous
// struct in two TUs would be an ODR violation; using the friendship
// from http_response.hpp lets us define it once per TU under the
// httpserver namespace, with internal linkage via an anonymous helper.
namespace httpserver {

// The friend struct in http_response.hpp is named
// http_response_sbo_test_access. Defining it here in the httpserver
// namespace gives this TU access to the private SBO state. This is the
// same pattern used by http_response_sbo_test.cpp; both TUs are
// build-tree-only test sources, never linked together, so there is no
// ODR conflict at link time.
struct http_response_sbo_test_access {
    static bool body_inline(http_response& r) noexcept {
        return r.body_inline_;
    }
    static httpserver::detail::body* body_ptr(http_response& r) noexcept {
        return r.body_;
    }
    static body_kind kind(http_response& r) noexcept { return r.kind_; }
};

}  // namespace httpserver

namespace {

using SBO = httpserver::http_response_sbo_test_access;

}  // namespace

LT_BEGIN_SUITE(http_response_factories_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http_response_factories_suite)

// -----------------------------------------------------------------------
// empty() — simplest factory; verifies kind() accessor + SBO placement.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite, empty_factory)
    http_response r = http_response::empty();
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::empty));
    LT_CHECK_EQ(SBO::body_inline(r), true);
    LT_ASSERT_NEQ(SBO::body_ptr(r),
                  static_cast<httpserver::detail::body*>(nullptr));
    // Default status code: 204 No Content (matches v1 empty_response).
    LT_CHECK_EQ(r.get_response_code(), 204);
LT_END_AUTO_TEST(empty_factory)

// -----------------------------------------------------------------------
// string() — covers AC #1 (kind() == body_kind::string).
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite, string_factory_kind)
    auto r = http_response::string("hi");
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::string));
    LT_CHECK_EQ(SBO::body_inline(r), true);
LT_END_AUTO_TEST(string_factory_kind)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   string_factory_default_content_type)
    auto r = http_response::string("hi");
    LT_CHECK_EQ(r.get_header("Content-Type"), std::string("text/plain"));
    LT_CHECK_EQ(r.get_response_code(), 200);
LT_END_AUTO_TEST(string_factory_default_content_type)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   string_factory_overridden_content_type)
    auto r = http_response::string("{}", "application/json");
    LT_CHECK_EQ(r.get_header("Content-Type"),
                std::string("application/json"));
LT_END_AUTO_TEST(string_factory_overridden_content_type)

// -----------------------------------------------------------------------
// file() — opens at construction, missing path doesn't throw.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite, file_factory_existing)
    // test_content lives in test/ — same fixture body_test uses.
    auto r = http_response::file("test_content");
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::file));
    LT_CHECK_EQ(SBO::body_inline(r), true);
    LT_CHECK_EQ(r.get_response_code(), 200);
LT_END_AUTO_TEST(file_factory_existing)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   file_factory_missing_path_does_not_throw)
    // Mirrors v1 file_response semantics: bad path is observable at
    // dispatch time (the materialized MHD_Response is null), not at
    // construction time.
    auto r = http_response::file("/no/such/path/should/exist");
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::file));
LT_END_AUTO_TEST(file_factory_missing_path_does_not_throw)

// -----------------------------------------------------------------------
// iovec() — covers AC #2 (compiles without <sys/uio.h>).
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite, iovec_factory_kind)
    static const char a[] = "abc";
    static const char b[] = "defg";
    std::array<httpserver::iovec_entry, 2> entries{{
        {a, 3},
        {b, 4},
    }};
    auto r = http_response::iovec(entries);
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::iovec));
    LT_CHECK_EQ(r.get_response_code(), 200);
LT_END_AUTO_TEST(iovec_factory_kind)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   iovec_factory_deep_copies_span)
    // Build a span over a temporary array; let the array go out of
    // scope before we observe r. The factory's deep-copy must keep the
    // body's iovec_entry vector valid.
    auto r = []() {
        std::array<httpserver::iovec_entry, 1> entries{{ {"x", 1} }};
        return http_response::iovec(entries);
    }();
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::iovec));
LT_END_AUTO_TEST(iovec_factory_deep_copies_span)

// -----------------------------------------------------------------------
// pipe() — owns the fd, destructor closes it when not materialized.
// Gated on !_WIN32 because Windows uses _pipe()/CreatePipe() rather
// than POSIX ::pipe(). See body_test.cpp for the same gate rationale.
// -----------------------------------------------------------------------
#ifndef _WIN32
LT_BEGIN_AUTO_TEST(http_response_factories_suite, pipe_factory_kind)
    int fds[2];
    int rc = ::pipe(fds);
    LT_ASSERT_EQ(rc, 0);
    {
        auto r = http_response::pipe(fds[0]);
        LT_CHECK_EQ(static_cast<int>(r.kind()),
                    static_cast<int>(body_kind::pipe));
        LT_CHECK_EQ(r.get_response_code(), 200);
    }
    // Destructor must have closed fds[0]; second close fails with EBADF.
    int second = ::close(fds[0]);
    LT_CHECK_EQ(second, -1);
    LT_CHECK_EQ(errno, EBADF);
    ::close(fds[1]);
LT_END_AUTO_TEST(pipe_factory_kind)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   pipe_factory_size_hint_is_accepted_but_ignored)
    // size_hint is reserved for future use; callers may pass it without
    // observable effect today.
    int fds[2];
    int rc = ::pipe(fds);
    LT_ASSERT_EQ(rc, 0);
    {
        auto r = http_response::pipe(fds[0], /*size_hint=*/4096);
        LT_CHECK_EQ(static_cast<int>(r.kind()),
                    static_cast<int>(body_kind::pipe));
    }
    ::close(fds[1]);
LT_END_AUTO_TEST(pipe_factory_size_hint_is_accepted_but_ignored)
#endif  // !_WIN32

// -----------------------------------------------------------------------
// deferred() — type-erased producer; sentinel test mirrors body_test.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite, deferred_factory_kind)
    auto r = http_response::deferred(
        [](std::uint64_t, char*, std::size_t) -> ssize_t {
            return MHD_CONTENT_READER_END_OF_STREAM;
        });
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::deferred));
    LT_CHECK_EQ(SBO::body_inline(r), true);
    LT_CHECK_EQ(r.get_response_code(), 200);
LT_END_AUTO_TEST(deferred_factory_kind)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   deferred_factory_releases_capture_on_destruction)
    auto sentinel = std::make_shared<int>(42);
    std::weak_ptr<int> w = sentinel;
    {
        auto r = http_response::deferred(
            [s = std::move(sentinel)](std::uint64_t, char*,
                                      std::size_t) -> ssize_t {
                (void)s;
                return MHD_CONTENT_READER_END_OF_STREAM;
            });
        LT_CHECK_EQ(w.expired(), false);
    }
    LT_CHECK_EQ(w.expired(), true);
LT_END_AUTO_TEST(deferred_factory_releases_capture_on_destruction)

// -----------------------------------------------------------------------
// unauthorized() — covers AC #3 (401 + WWW-Authenticate header).
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_basic_status_and_header)
    auto r = http_response::unauthorized("Basic", "myrealm");
    LT_CHECK_EQ(r.get_response_code(),
                httpserver::http::http_utils::http_unauthorized);
    LT_CHECK_EQ(r.get_response_code(), 401);
    // AC requires byte-for-byte match.
    LT_CHECK_EQ(r.get_header(httpserver::http::http_utils::http_header_www_authenticate),
                std::string(R"(Basic realm="myrealm")"));
LT_END_AUTO_TEST(unauthorized_basic_status_and_header)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_digest_scheme_renders_in_header)
    auto r = http_response::unauthorized("Digest", "myrealm");
    LT_CHECK_EQ(r.get_header(httpserver::http::http_utils::http_header_www_authenticate),
                std::string(R"(Digest realm="myrealm")"));
LT_END_AUTO_TEST(unauthorized_digest_scheme_renders_in_header)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_kind_is_string_even_when_body_empty)
    // The body slot literally holds a string_body (with empty content)
    // so kind() must report body_kind::string. Forking on the empty
    // case to report body_kind::empty would break the invariant.
    auto r = http_response::unauthorized("Basic", "myrealm");
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::string));
LT_END_AUTO_TEST(unauthorized_kind_is_string_even_when_body_empty)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_with_explicit_body)
    auto r = http_response::unauthorized("Basic", "myrealm",
                                         "please log in");
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::string));
    LT_CHECK_EQ(r.get_response_code(), 401);
LT_END_AUTO_TEST(unauthorized_with_explicit_body)

// -----------------------------------------------------------------------
// unauthorized() — header injection validation (security-reviewer-iter1-1,
// security-reviewer-iter1-2). CRLF sequences in scheme or realm must be
// rejected (std::invalid_argument) to prevent header injection (CWE-113).
// Double-quotes embedded in realm must be escaped per RFC 7235 §2.1
// (backslash-escape) so the quoted-string is syntactically valid.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_crlf_in_scheme_throws)
    // CRLF in scheme must throw — caller error, not a runtime failure.
    bool caught = false;
    try {
        auto r = http_response::unauthorized("Basic\r\nX-Injected: hdr",
                                             "myrealm");
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(unauthorized_crlf_in_scheme_throws)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_lf_in_scheme_throws)
    bool caught = false;
    try {
        auto r = http_response::unauthorized("Basic\nEvil: hdr", "myrealm");
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(unauthorized_lf_in_scheme_throws)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_cr_in_scheme_throws)
    bool caught = false;
    try {
        auto r = http_response::unauthorized("Basic\r", "myrealm");
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(unauthorized_cr_in_scheme_throws)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_nul_in_scheme_throws)
    // NUL in scheme is equally dangerous — reject it.
    bool caught = false;
    try {
        std::string s("Basic");
        s.push_back('\0');
        s += "evil";
        auto r = http_response::unauthorized(std::string_view(s.data(),
                                                               s.size()),
                                             "myrealm");
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(unauthorized_nul_in_scheme_throws)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_crlf_in_realm_throws)
    bool caught = false;
    try {
        auto r = http_response::unauthorized(
            "Basic", "evil\r\nX-Injected: hdr");
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(unauthorized_crlf_in_realm_throws)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_lf_in_realm_throws)
    bool caught = false;
    try {
        auto r = http_response::unauthorized("Basic", "evil\nMore: hdr");
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(unauthorized_lf_in_realm_throws)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_nul_in_realm_throws)
    bool caught = false;
    try {
        std::string realm("my");
        realm.push_back('\0');
        realm += "realm";
        auto r = http_response::unauthorized(
            "Basic", std::string_view(realm.data(), realm.size()));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(unauthorized_nul_in_realm_throws)

LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   unauthorized_double_quote_in_realm_is_escaped)
    // RFC 7235 §2.1: double-quotes inside a quoted-string must be
    // backslash-escaped.  realm=foo"bar must produce
    // WWW-Authenticate: Basic realm="foo\"bar"
    auto r = http_response::unauthorized("Basic", R"(foo"bar)");
    LT_CHECK_EQ(
        r.get_header(httpserver::http::http_utils::http_header_www_authenticate),
        std::string(R"(Basic realm="foo\"bar")"));
LT_END_AUTO_TEST(unauthorized_double_quote_in_realm_is_escaped)

// -----------------------------------------------------------------------
// Move smoke: factory results survive being returned from a function.
// Protects against a future regression of the noexcept move ctor.
// -----------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_factories_suite,
                   factory_move_preserves_kind_and_headers)
    auto make = []() {
        return http_response::string("payload", "text/html");
    };
    http_response r = make();
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::string));
    LT_CHECK_EQ(r.get_header("Content-Type"), std::string("text/html"));
    LT_CHECK_EQ(r.get_response_code(), 200);

    // And one move-assign.
    http_response other = http_response::empty();
    other = std::move(r);
    LT_CHECK_EQ(static_cast<int>(other.kind()),
                static_cast<int>(body_kind::string));
    LT_CHECK_EQ(other.get_response_code(), 200);
LT_END_AUTO_TEST(factory_move_preserves_kind_and_headers)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
