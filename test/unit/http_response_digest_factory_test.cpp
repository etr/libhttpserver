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

// TASK-062 unit test: RFC-7616 Digest auth factory `http_response::unauthorized(digest_challenge)`.
//
// Pins the public-observable contract of the new `digest_challenge`
// overload of `http_response::unauthorized`:
//   * status 401, kind == body_kind::digest_challenge;
//   * algorithm/qop fields round-trip through the factory and are
//     observable via the test-access friend hook;
//   * CR/LF/NUL in realm/opaque/domain throw std::invalid_argument
//     (header-injection guard, mirrors the Basic-side validation);
//   * the new digest_challenge_body subclass fits the 64-byte SBO
//     budget (pinned by the static_assert in detail/body.hpp, mirrored
//     in-test to catch a future ABI drift early).
//
// The end-to-end byte-format assertions for the WWW-Authenticate
// challenge produced on the wire live in the new integ test
// digest_challenge_format_test.cpp -- unit tests cannot observe the
// MHD_queue_auth_required_response3 output without a live MHD daemon.

#include <microhttpd.h>

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "./httpserver.hpp"                 // public umbrella
#include "httpserver/detail/body.hpp"       // private detail::body (test-only)
#include "./littletest.hpp"

using httpserver::body_kind;
using httpserver::http_response;
using httpserver::digest_challenge;
using httpserver::http::http_utils;

namespace httpserver {

// Mirrors the friend struct in http_response_factories_test.cpp; both
// TUs are build-tree-only test sources and never linked together, so
// there is no ODR conflict. We only need the kind() peek for the SBO
// flag here.
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

LT_BEGIN_SUITE(http_response_digest_factory_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http_response_digest_factory_suite)

#ifdef HAVE_DAUTH

// ---------------------------------------------------------------------
// SBO budget pin: digest_challenge_body must fit in http_response's
// 64-byte inline buffer with alignment <= 16.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_body_fits_sbo)
    LT_CHECK_EQ(sizeof(httpserver::detail::digest_challenge_body) <= 64,
                true);
    LT_CHECK_EQ(alignof(httpserver::detail::digest_challenge_body) <= 16,
                true);
LT_END_AUTO_TEST(digest_challenge_body_fits_sbo)

// ---------------------------------------------------------------------
// Defaults (RFC 7616 §3.3 minimal challenge).
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_status_and_kind)
    digest_challenge ch;
    ch.realm = "testrealm@host.com";
    auto r = http_response::unauthorized(std::move(ch));
    LT_CHECK_EQ(r.get_status(), 401);
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::digest_challenge));
    LT_CHECK_EQ(SBO::body_inline(r), true);
LT_END_AUTO_TEST(digest_challenge_status_and_kind)

// ---------------------------------------------------------------------
// The body slot stores parameters; the *MHD-side* WWW-Authenticate
// header is NOT pre-attached to the response value -- that header is
// added by libmicrohttpd inside MHD_queue_auth_required_response3 at
// dispatch time. Verifying its absence here protects against a
// regression where someone "helpfully" adds a duplicate (malformed)
// header at the response-value layer.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_does_not_set_www_authenticate_header)
    digest_challenge ch;
    ch.realm = "testrealm@host.com";
    auto r = http_response::unauthorized(std::move(ch));
    LT_CHECK_EQ(std::string(r.get_header(
                    http_utils::http_header_www_authenticate)),
                std::string(""));
LT_END_AUTO_TEST(digest_challenge_does_not_set_www_authenticate_header)

// ---------------------------------------------------------------------
// Algorithm round-trip: MD5 (default), SHA256.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_default_algo_is_md5)
    digest_challenge ch;
    ch.realm = "r";
    auto r = http_response::unauthorized(std::move(ch));
    auto* body = static_cast<httpserver::detail::digest_challenge_body*>(
        SBO::body_ptr(r));
    LT_ASSERT_NEQ(body,
        static_cast<httpserver::detail::digest_challenge_body*>(nullptr));
    LT_CHECK_EQ(static_cast<int>(body->get_params().algorithm),
                static_cast<int>(http_utils::digest_algorithm::MD5));
LT_END_AUTO_TEST(digest_challenge_default_algo_is_md5)

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_sha256_algorithm)
    digest_challenge ch;
    ch.realm = "r";
    ch.algorithm = http_utils::digest_algorithm::SHA256;
    auto r = http_response::unauthorized(std::move(ch));
    auto* body = static_cast<httpserver::detail::digest_challenge_body*>(
        SBO::body_ptr(r));
    LT_ASSERT_NEQ(body,
        static_cast<httpserver::detail::digest_challenge_body*>(nullptr));
    LT_CHECK_EQ(static_cast<int>(body->get_params().algorithm),
                static_cast<int>(http_utils::digest_algorithm::SHA256));
LT_END_AUTO_TEST(digest_challenge_sha256_algorithm)

// ---------------------------------------------------------------------
// Opaque/domain round-trip.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_opaque_and_domain_roundtrip)
    digest_challenge ch;
    ch.realm = "r";
    ch.opaque = "abc123";
    ch.domain = "/protected /other";
    auto r = http_response::unauthorized(std::move(ch));
    auto* body = static_cast<httpserver::detail::digest_challenge_body*>(
        SBO::body_ptr(r));
    LT_ASSERT_NEQ(body,
        static_cast<httpserver::detail::digest_challenge_body*>(nullptr));
    LT_CHECK_EQ(body->get_params().opaque, std::string("abc123"));
    LT_CHECK_EQ(body->get_params().domain,
                std::string("/protected /other"));
LT_END_AUTO_TEST(digest_challenge_opaque_and_domain_roundtrip)

// ---------------------------------------------------------------------
// Body text round-trip.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_body_text_roundtrip)
    digest_challenge ch;
    ch.realm = "r";
    ch.body = "access denied";
    auto r = http_response::unauthorized(std::move(ch));
    auto* body = static_cast<httpserver::detail::digest_challenge_body*>(
        SBO::body_ptr(r));
    LT_ASSERT_NEQ(body,
        static_cast<httpserver::detail::digest_challenge_body*>(nullptr));
    LT_CHECK_EQ(body->size(), std::size_t{13});
LT_END_AUTO_TEST(digest_challenge_body_text_roundtrip)

// ---------------------------------------------------------------------
// Header injection guards: CR/LF/NUL in realm/opaque/domain throw.
// These mirror the existing Basic-side validation
// (unauthorized_crlf_in_realm_throws in http_response_factories_test).
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_crlf_in_realm_throws)
    bool caught = false;
    try {
        digest_challenge ch;
        ch.realm = "evil\r\nX-Injected: hdr";
        auto r = http_response::unauthorized(std::move(ch));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(digest_challenge_crlf_in_realm_throws)

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_crlf_in_opaque_throws)
    bool caught = false;
    try {
        digest_challenge ch;
        ch.realm = "r";
        ch.opaque = "evil\r\nX-Injected: hdr";
        auto r = http_response::unauthorized(std::move(ch));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(digest_challenge_crlf_in_opaque_throws)

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_lf_in_domain_throws)
    bool caught = false;
    try {
        digest_challenge ch;
        ch.realm = "r";
        ch.domain = "/p\nEvil: 1";
        auto r = http_response::unauthorized(std::move(ch));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(digest_challenge_lf_in_domain_throws)

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_nul_in_realm_throws)
    bool caught = false;
    try {
        digest_challenge ch;
        std::string r_realm("realm");
        r_realm.push_back('\0');
        r_realm += "evil";
        ch.realm = r_realm;
        auto r = http_response::unauthorized(std::move(ch));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(digest_challenge_nul_in_realm_throws)

// ---------------------------------------------------------------------
// Backward compatibility: the existing string-based
// unauthorized("Digest", realm) overload continues to render the v1
// static challenge byte-for-byte (test/unit/http_response_factories_test
// :unauthorized_digest_scheme_renders_in_header). This test pins that
// the v1 path still produces body_kind::string (NOT digest_challenge),
// so a caller relying on the legacy surface is unaffected.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   legacy_string_overload_still_uses_string_body)
    auto r = http_response::unauthorized("Digest", "examplerealm");
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::string));
    LT_CHECK_EQ(r.get_status(), 401);
LT_END_AUTO_TEST(legacy_string_overload_still_uses_string_body)

#else  // !HAVE_DAUTH

// Sentinel: HAVE_DAUTH guards the digest_challenge overload at the
// public-header level; with HAVE_DAUTH off the test compiles to a no-op.
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite, dauth_disabled_noop)
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(dauth_disabled_noop)

#endif  // HAVE_DAUTH

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
