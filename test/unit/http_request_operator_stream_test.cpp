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

// Secure-by-default redaction of credential material in
// http_request::operator<<. Default-built test requests must NOT leak
// the Basic-auth password, Authorization / Proxy-Authorization header
// values, or any cookie value into the diagnostic dump. The opt-in
// create_webserver::expose_credentials_in_logs(true) flag (propagated
// through create_test_request::expose_credentials_in_logs() for unit
// scope) restores the v1 verbose form for development.

// HAVE_BAUTH gates the username / password surfaces that operator<<
// streams. On HAVE_BAUTH-off builds (Windows MSYS2 lane, flag-invariance-
// off lane) get_user() / get_pass() return empty string_views by
// contract, so the user:"admin" / pass:"hunter2" expectations below do
// not hold; the BAUTH-off build path is covered by
// webserver_dauth_unavailable / webserver_features tests instead.
// HAVE_BAUTH is forwarded into the test compile via -DHAVE_BAUTH on the
// libtool command line; no explicit <config.h> include is needed (and
// would clash with the -DDEBUG flag autotools sets on debug lanes,
// since config.h re-defines DEBUG).

#include <sstream>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/create_test_request.hpp"
#include "./littletest.hpp"

using httpserver::create_test_request;
using httpserver::http_request;

LT_BEGIN_SUITE(http_request_operator_stream_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(http_request_operator_stream_suite)

#ifdef HAVE_BAUTH
// Acceptance: default-built http_request must redact
// credentials when streamed via operator<<.
LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite, operator_stream_redacts_credentials)
    auto req = create_test_request()
        // Fictitious test fixture — not a real credential.
        .user("admin")
        .pass("hunter2")
        .header("Authorization", "Basic YWRtaW46aHVudGVyMg==")
        .header("Proxy-Authorization", "Digest username=\"admin\", response=\"deadbeef\"")
        .footer("Authorization", "Bearer footertoken")
        .cookie("session", "session-token-cafef00d")
        .cookie("csrf", "csrf-token-abad1dea")
        .arg("page", "2")
        .build();

    std::ostringstream oss;
    oss << req;
    const std::string out = oss.str();

    // Username remains visible (REMOTE_USER access-log convention).
    LT_CHECK(out.find("user:\"admin\"") != std::string::npos);

    // Basic-auth password value must be redacted.
    LT_CHECK(out.find("pass:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("hunter2") == std::string::npos);

    // Authorization-class header values must be redacted; the base64
    // and digest payloads must NOT appear.
    LT_CHECK(out.find("Authorization:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("Proxy-Authorization:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("YWRtaW46aHVudGVyMg==") == std::string::npos);
    LT_CHECK(out.find("deadbeef") == std::string::npos);

    // Footer Authorization values share the header redaction path.
    LT_CHECK(out.find("footertoken") == std::string::npos);

    // Every cookie value must be redacted; cookie keys remain visible.
    LT_CHECK(out.find("session:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("csrf:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("session-token-cafef00d") == std::string::npos);
    LT_CHECK(out.find("csrf-token-abad1dea") == std::string::npos);

    // Query-string arguments are streamed verbatim regardless of the
    // expose flag — guards against accidental over-redaction in the
    // Args section if the operator<< body is ever refactored.
    LT_CHECK(out.find("page") != std::string::npos);
LT_END_AUTO_TEST(operator_stream_redacts_credentials)

// Acceptance: opt-in flag (development-only) restores the
// v1 verbose form bit-for-bit. The redaction token MUST NOT appear in
// the output when the opt-in is set.
LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite, operator_stream_exposes_credentials_when_opted_in)
    auto req = create_test_request()
        // Fictitious test fixture — not a real credential.
        .user("admin")
        .pass("hunter2")
        .header("Authorization", "Basic YWRtaW46aHVudGVyMg==")
        .header("Proxy-Authorization", "Digest username=\"admin\", response=\"deadbeef\"")
        .cookie("session", "session-token-cafef00d")
        .cookie("csrf", "csrf-token-abad1dea")
        .expose_credentials_in_logs()
        .build();

    std::ostringstream oss;
    oss << req;
    const std::string out = oss.str();

    // Verbose v1 form: every credential surface is streamed plaintext.
    LT_CHECK(out.find("pass:\"hunter2\"") != std::string::npos);
    LT_CHECK(out.find("Authorization:\"Basic YWRtaW46aHVudGVyMg==\"") != std::string::npos);
    // Asserting on the exact nested-quote serialisation would couple
    // the test to the quoting strategy. Check the observable security
    // property instead: the key is present and the credential payload
    // is streamed verbatim.
    LT_CHECK(out.find("Proxy-Authorization:") != std::string::npos);
    LT_CHECK(out.find("deadbeef") != std::string::npos);
    LT_CHECK(out.find("session:\"session-token-cafef00d\"") != std::string::npos);
    LT_CHECK(out.find("csrf:\"csrf-token-abad1dea\"") != std::string::npos);

    // The redaction token MUST NOT appear in the dump when the opt-in
    // flag is set (lets a developer inspect verbatim wire payloads).
    LT_CHECK(out.find("<redacted>") == std::string::npos);
LT_END_AUTO_TEST(operator_stream_exposes_credentials_when_opted_in)
#else
// HAVE_BAUTH-off complement of the redaction tests
// above. On this build the user/pass surfaces are absent (get_user() /
// get_pass() return empty views per src/http_request_auth.cpp), but the
// Authorization-class header redaction and cookie redaction are
// independent of HAVE_BAUTH (dump_header_map_redacted /
// dump_cookie_map_redacted at src/http_request.cpp:431, :444 — neither
// has a HAVE_BAUTH gate). The on-build expectation that
// .user("admin") / .pass("hunter2") render verbatim does NOT hold here:
// the builder calls become no-ops at the field level, and the streamed
// line is `user:"" pass:"<redacted>"`. The Authorization and Cookie
// redaction continues to fire.
namespace {
// Shared by the three HAVE_BAUTH-off tests below: stream a request
// through operator<< and return the resulting string.
auto dump = [](const httpserver::http_request& r) {
    std::ostringstream o;
    o << r;
    return o.str();
};
}  // namespace

LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite,
                   operator_stream_credential_surfaces_absent_on_bauth_off)
    auto req = create_test_request()
        .user("admin")
        .pass("hunter2")
        .build();

    const std::string out = dump(req);

    // user/pass field values must not leak — the surfaces are absent on
    // a HAVE_BAUTH-off build by construction.
    LT_CHECK(out.find("admin") == std::string::npos);
    LT_CHECK(out.find("hunter2") == std::string::npos);

    // The user field is emitted with an empty value on this build.
    LT_CHECK(out.find("user:\"\"") != std::string::npos);

    // The pass redaction token still appears (the field is emitted
    // unconditionally; the value is the redacted token). Both fields
    // are always streamed regardless of HAVE_BAUTH or the expose flag:
    // see src/http_request.cpp:465-467 (the expose ternary at
    // lines 462-464 only changes pass_out's value, not whether the
    // fields are emitted).
    LT_CHECK(out.find("pass:\"<redacted>\"") != std::string::npos);
LT_END_AUTO_TEST(operator_stream_credential_surfaces_absent_on_bauth_off)

LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite,
                   operator_stream_redacts_authorization_header_on_bauth_off)
    auto req = create_test_request()
        .header("Authorization", "Basic YWRtaW46aHVudGVyMg==")
        .header("Proxy-Authorization", "Digest username=\"admin\", response=\"deadbeef\"")
        .footer("Authorization", "Bearer footertoken")
        .build();

    const std::string out = dump(req);

    // Authorization-class header redaction is independent of HAVE_BAUTH
    // (dump_header_map_redacted has no HAVE_BAUTH gate). The credential
    // payload must NOT leak; the redaction token must appear in its place.
    LT_CHECK(out.find("Authorization:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("Proxy-Authorization:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("YWRtaW46aHVudGVyMg==") == std::string::npos);
    LT_CHECK(out.find("deadbeef") == std::string::npos);
    LT_CHECK(out.find("footertoken") == std::string::npos);
LT_END_AUTO_TEST(operator_stream_redacts_authorization_header_on_bauth_off)

LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite,
                   operator_stream_redacts_cookies_on_bauth_off)
    auto req = create_test_request()
        .cookie("session", "session-token-cafef00d")
        .cookie("csrf", "csrf-token-abad1dea")
        .build();

    const std::string out = dump(req);

    // Cookie redaction is independent of HAVE_BAUTH
    // (dump_cookie_map_redacted has no HAVE_BAUTH gate). Every cookie
    // value must be redacted; cookie keys remain visible.
    LT_CHECK(out.find("session:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("csrf:\"<redacted>\"") != std::string::npos);
    LT_CHECK(out.find("session-token-cafef00d") == std::string::npos);
    LT_CHECK(out.find("csrf-token-abad1dea") == std::string::npos);
LT_END_AUTO_TEST(operator_stream_redacts_cookies_on_bauth_off)
#endif  // HAVE_BAUTH

// Edge case: the no-credentials request still emits the
// `pass:"<redacted>"` token (because the field is unconditional), but
// no Cookies section appears (the redacted dumper short-circuits on
// empty maps) and no header value collides with the redaction token.
LT_BEGIN_AUTO_TEST(http_request_operator_stream_suite, operator_stream_no_credentials)
    auto req = create_test_request()
        .method("GET")
        .path("/health")
        .build();

    std::ostringstream oss;
    oss << req;
    const std::string out = oss.str();

    // The pass field always emits, even when no Basic-auth password was
    // set, so callers can rely on a uniform shape.
    LT_CHECK(out.find("pass:\"<redacted>\"") != std::string::npos);

    // Empty cookie and footer maps must NOT cause a stray `<redacted>`
    // entry to appear in any section.
    LT_CHECK(out.find("Cookies") == std::string::npos);
    LT_CHECK(out.find("Footers") == std::string::npos);

    // Placeholder secrets from the populated-request tests must not
    // leak in via shared state or test-runner residue.
    LT_CHECK(out.find("hunter2") == std::string::npos);
LT_END_AUTO_TEST(operator_stream_no_credentials)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
