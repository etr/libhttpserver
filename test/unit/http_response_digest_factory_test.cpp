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

// Unit test: RFC-7616 Digest auth factory `http_response::unauthorized(digest_challenge)`.
//
// Pins the public-observable contract of the new `digest_challenge`
// overload of `http_response::unauthorized`:
//   * status 401, kind == body_kind::digest_challenge (regardless of
//     algorithm/opaque/domain/body-text values);
//   * CR/LF/NUL in realm/opaque/domain throw std::invalid_argument
//     (header-injection guard, mirrors the Basic-side validation);
//   * the new digest_challenge_response_body subclass fits the 64-byte SBO
//     budget (pinned by the static_assert in detail/body.hpp, mirrored
//     in-test to catch a future ABI drift early).
//
// The unit tests do NOT reach into detail::digest_challenge_response_body via the
// friend hook to inspect get_params() fields (algorithm, opaque, domain).
// Those fields are verified at the wire level in:
//   test/integ/digest_challenge_format_test.cpp
// which asserts on the actual WWW-Authenticate header emitted by MHD.
// Keeping internal-struct assertions out of unit tests avoids coupling to
// the current storage shape.
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
#include "httpserver/detail/body.hpp"       // private detail::response_body (test-only)
#include "./littletest.hpp"

using httpserver::body_kind;
using httpserver::http_response;
using httpserver::digest_challenge;
using httpserver::http::http_utils;

namespace httpserver {

// Mirrors the friend struct in http_response_factories_test.cpp; both
// TUs are build-tree-only test sources and never linked together, so
// there is no ODR conflict. We only need the body_inline() flag here
// to assert the SBO budget (body placed inline vs. heap pointer).
struct http_response_sbo_test_access {
    static bool body_inline(http_response& r) noexcept {
        return r.body_inline_;
    }
    static httpserver::detail::response_body* body_ptr(http_response& r) noexcept {
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
// SBO budget pin: digest_challenge_response_body must fit in http_response's
// 64-byte inline buffer with alignment <= 16.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_body_fits_sbo)
    LT_CHECK_EQ(sizeof(httpserver::detail::digest_challenge_response_body) <= 64,
                true);
    LT_CHECK_EQ(alignof(httpserver::detail::digest_challenge_response_body) <= 16,
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
// Algorithm and field round-trips: body_kind discrimination only.
//
// The previous tests in this section reached into the private
// detail::digest_challenge_response_body params struct via the SBO friend hook and
// a concrete-subclass cast (body->get_params().algorithm, .opaque, .domain).
// That couples the unit tests to the internal storage shape.  The
// authoritative place to verify algorithm, opaque, and domain values is
// the wire-format integration test (digest_challenge_format_test.cpp),
// which asserts on the actual WWW-Authenticate header emitted by MHD.
//
// Here we pin only what is observable without internal access:
//   * the factory produces body_kind::digest_challenge regardless of
//     which algorithm/fields are set (already covered by
//     digest_challenge_status_and_kind above, repeated here for
//     algorithm=SHA256 and for opaque+domain to protect the factory
//     code paths without re-coupling to internals).
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_sha256_produces_digest_challenge_kind)
    // Observable contract: kind() == digest_challenge regardless of
    // which algorithm is selected.  Wire-format coverage (SHA-256 token
    // in WWW-Authenticate) lives in digest_challenge_format_test.cpp:
    //   rfc7616_challenge_sha256_algorithm_in_header.
    digest_challenge ch;
    ch.realm = "r";
    ch.algorithm = http_utils::digest_algorithm::SHA256;
    auto r = http_response::unauthorized(std::move(ch));
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::digest_challenge));
    LT_CHECK_EQ(r.get_status(), 401);
LT_END_AUTO_TEST(digest_challenge_sha256_produces_digest_challenge_kind)

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_sha512_256_produces_digest_challenge_kind)
    // Observable contract: kind() == digest_challenge regardless of
    // which algorithm is selected (mirrors the SHA-256 test above for the
    // SHA512_256 enumerator).
    digest_challenge ch;
    ch.realm = "r";
    ch.algorithm = http_utils::digest_algorithm::SHA512_256;
    auto r = http_response::unauthorized(std::move(ch));
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::digest_challenge));
    LT_CHECK_EQ(r.get_status(), 401);
LT_END_AUTO_TEST(digest_challenge_sha512_256_produces_digest_challenge_kind)

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_opaque_and_domain_produce_digest_challenge_kind)
    // Observable contract: kind() == digest_challenge when opaque and
    // domain are set.  Wire-format coverage lives in
    // digest_challenge_format_test.cpp:
    //   rfc7616_challenge_opaque_and_domain_in_header.
    digest_challenge ch;
    ch.realm = "r";
    ch.opaque = "abc123";
    ch.domain = "/protected /other";
    auto r = http_response::unauthorized(std::move(ch));
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::digest_challenge));
    LT_CHECK_EQ(r.get_status(), 401);
LT_END_AUTO_TEST(digest_challenge_opaque_and_domain_produce_digest_challenge_kind)

// ---------------------------------------------------------------------
// Body text: verify the factory accepts a non-empty body string and
// still produces a digest_challenge kind response.  The byte count is
// not pinned here (internals); a non-empty body round-trip at the wire
// level is already exercised by rfc7616_challenge_carries_required_fields.
// ---------------------------------------------------------------------
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_with_body_text_is_digest_challenge_kind)
    digest_challenge ch;
    ch.realm = "r";
    ch.response_body = "access denied";
    auto r = http_response::unauthorized(std::move(ch));
    LT_CHECK_EQ(static_cast<int>(r.kind()),
                static_cast<int>(body_kind::digest_challenge));
    LT_CHECK_EQ(r.get_status(), 401);
LT_END_AUTO_TEST(digest_challenge_with_body_text_is_digest_challenge_kind)

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

// qop="auth-int" has no dispatch mapping (map_to_mhd_digest_args_
// ignores it), so the factory rejects it loudly instead of letting a
// caller believe integrity protection was negotiated.
LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_qop_auth_int_throws)
    bool caught = false;
    try {
        digest_challenge ch;
        ch.realm = "r";
        ch.qop_auth_int = true;
        auto r = http_response::unauthorized(std::move(ch));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(digest_challenge_qop_auth_int_throws)

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

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_nul_in_opaque_throws)
    bool caught = false;
    try {
        digest_challenge ch;
        ch.realm = "r";
        std::string r_opaque("opaque");
        r_opaque.push_back('\0');
        r_opaque += "evil";
        ch.opaque = r_opaque;
        auto r = http_response::unauthorized(std::move(ch));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(digest_challenge_nul_in_opaque_throws)

LT_BEGIN_AUTO_TEST(http_response_digest_factory_suite,
                   digest_challenge_nul_in_domain_throws)
    bool caught = false;
    try {
        digest_challenge ch;
        ch.realm = "r";
        std::string r_domain("/protected");
        r_domain.push_back('\0');
        r_domain += "evil";
        ch.domain = r_domain;
        auto r = http_response::unauthorized(std::move(ch));
        (void)r;
    } catch (const std::invalid_argument&) {
        caught = true;
    }
    LT_CHECK_EQ(caught, true);
LT_END_AUTO_TEST(digest_challenge_nul_in_domain_throws)

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
