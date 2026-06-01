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

// TASK-058 step 2: pin that auth_skip_paths entries are normalized at
// webserver construction time, so non-canonical inputs ({"/public/",
// "/a/../b", "/x/./y"}) match canonical request paths ({"/public",
// "/b", "/x/y"}).
//
// Pre-TASK-058: should_skip_auth normalized the *request* path but
// compared it verbatim against the skip list, so a non-canonical skip-
// list entry never matched.  This was a latent bug -- callers who
// passed pretty-but-non-canonical skip paths (e.g. trailing slashes
// from copy-paste, "/foo/./bar" from path-builder code) silently saw
// their auth-skip preference ignored.
//
// After step 2 the skip list is pre-normalized at construction; this
// test pins the new behaviour and also covers the empty-list early-
// out path (no skip paths configured -> should_skip_auth returns
// false without touching normalize_path).

#include <memory>
#include <string>
#include <vector>

#include "./httpserver.hpp"
#include "./httpserver/detail/webserver_impl.hpp"
#include "./littletest.hpp"

namespace ht = httpserver;

namespace {

ht::detail::webserver_impl& impl_of(ht::webserver& ws) {
    return *ht::webserver_test_access::impl(ws);
}

}  // namespace

LT_BEGIN_SUITE(auth_skip_normalize_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(auth_skip_normalize_suite)

// ---------------------------------------------------------------------
// Non-canonical skip paths with trailing slashes match canonical
// request paths after construction-time normalization.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   trailing_slash_skip_path_matches_canonical_request)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)
        .auth_skip_paths({"/public/"})};

    // Canonical request path -- pre-normalize on skip-list side makes
    // this match.
    LT_CHECK(impl_of(ws).should_skip_auth("/public"));
LT_END_AUTO_TEST(trailing_slash_skip_path_matches_canonical_request)

// ---------------------------------------------------------------------
// ".." segments in skip paths are collapsed at construction time.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   dotdot_skip_path_matches_canonical_request)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)
        .auth_skip_paths({"/a/../b"})};

    LT_CHECK(impl_of(ws).should_skip_auth("/b"));
LT_END_AUTO_TEST(dotdot_skip_path_matches_canonical_request)

// ---------------------------------------------------------------------
// "." segments in skip paths are stripped at construction time.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   dot_skip_path_matches_canonical_request)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)
        .auth_skip_paths({"/x/./y"})};

    LT_CHECK(impl_of(ws).should_skip_auth("/x/y"));
LT_END_AUTO_TEST(dot_skip_path_matches_canonical_request)

// ---------------------------------------------------------------------
// Already-canonical skip paths continue to work (regression net for
// the existing happy path).
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   canonical_skip_path_still_matches)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)
        .auth_skip_paths({"/public", "/health"})};

    LT_CHECK(impl_of(ws).should_skip_auth("/public"));
    LT_CHECK(impl_of(ws).should_skip_auth("/health"));
    LT_CHECK(!impl_of(ws).should_skip_auth("/api"));
LT_END_AUTO_TEST(canonical_skip_path_still_matches)

// ---------------------------------------------------------------------
// Wildcard-suffix skip paths ("/public/*") have their prefix
// normalized too; the wildcard semantics are preserved.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   wildcard_suffix_skip_path_normalizes_prefix)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)
        .auth_skip_paths({"/public/*"})};

    LT_CHECK(impl_of(ws).should_skip_auth("/public/foo"));
    LT_CHECK(impl_of(ws).should_skip_auth("/public/foo/bar"));
    LT_CHECK(!impl_of(ws).should_skip_auth("/private/foo"));
LT_END_AUTO_TEST(wildcard_suffix_skip_path_normalizes_prefix)

// ---------------------------------------------------------------------
// Empty skip list early-out: no auth_skip_paths configured, so
// should_skip_auth must return false without touching the per-request
// normalize machinery.  Behavior pin only -- the perf win is visible
// in the bench, not here.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   empty_skip_list_returns_false_for_any_path)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)};

    LT_CHECK(!impl_of(ws).should_skip_auth("/anywhere"));
    LT_CHECK(!impl_of(ws).should_skip_auth("/"));
    LT_CHECK(!impl_of(ws).should_skip_auth(""));
LT_END_AUTO_TEST(empty_skip_list_returns_false_for_any_path)

// ---------------------------------------------------------------------
// TASK-058 / security-reviewer-iter1-1: global wildcard "/*" must skip
// auth for every path.  Pre-fix the size() > 2 guard in should_skip_auth
// excluded "/*" (size == 2), so it fell through to an exact-equality
// check that never matched, silently doing nothing.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   global_wildcard_skip_path_matches_any_path)
    ht::webserver ws{ht::create_webserver(8080)
        .start_method(ht::http::http_utils::INTERNAL_SELECT)
        .auth_skip_paths({"/*"})};

    LT_CHECK(impl_of(ws).should_skip_auth("/"));
    LT_CHECK(impl_of(ws).should_skip_auth("/anything"));
    LT_CHECK(impl_of(ws).should_skip_auth("/foo/bar/baz"));
LT_END_AUTO_TEST(global_wildcard_skip_path_matches_any_path)

// ---------------------------------------------------------------------
// TASK-058 / security-reviewer-iter1-3: percent-encoded entries in
// auth_skip_paths must be rejected at construction time.  Passing a
// '%'-containing entry is a misconfiguration (the entry would silently
// never match a decoded request path).  The expectation is that
// normalize_auth_skip_paths throws std::invalid_argument.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(auth_skip_normalize_suite,
                   percent_encoded_skip_path_throws_invalid_argument)
    LT_CHECK_THROW(
        ht::webserver{ht::create_webserver(8080)
            .start_method(ht::http::http_utils::INTERNAL_SELECT)
            .auth_skip_paths({"/public%2Ftest"})});
LT_END_AUTO_TEST(percent_encoded_skip_path_throws_invalid_argument)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
