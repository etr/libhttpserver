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

// TASK-034 cycle F + TASK-037: compile-only consumer fixture.
//
// A single .cpp file that touches every public symbol whose declaration
// was previously guarded by #ifdef HAVE_* in the public headers. It MUST
// compile (and link) with the library no matter which combination of
// HAVE_BAUTH / HAVE_DAUTH / HAVE_GNUTLS / HAVE_WEBSOCKET the library was
// built with. AC #2 of TASK-034: "a consumer source file compiles
// unchanged against TLS-on and TLS-off builds" (and, by extension, the
// same for the other three flags).
//
// Runtime behaviour is irrelevant here: main() returns 0 immediately.
// The point is the compile + link, exercised in CI by the TASK-037
// build-flag-invariance matrix entries (.github/workflows/verify-build.yml).
// TASK-037 extends the original fixture to also pin (a) every remaining
// TLS cert accessor declared in http_request.hpp, (b) the DAUTH-gated
// check_digest_auth() declaration via a member-function pointer (without
// invoking it on a TLS-off build), and (c) the positive-`true` form of
// the use_ssl / basic_auth / digest_auth setters on create_webserver,
// again via member-function-pointer references so the fixture remains
// constructor-safe in every HAVE_* combination.

#include <cstddef>
#include <memory>
#include <string>

#include "./httpserver.hpp"
// create_test_request isn't pulled in by the umbrella header (it lives
// under the test-helper banner), so include it explicitly. AM_CPPFLAGS
// already passes -DHTTPSERVER_COMPILATION, so the gate at the top of
// the header is satisfied.
#include "httpserver/create_test_request.hpp"

namespace fixture {

httpserver::webserver build_server() {
    httpserver::create_webserver cw{8080};
    cw.use_ssl(false);    // always callable (was #ifdef HAVE_GNUTLS implicitly)
    cw.basic_auth(false);  // always callable (was #ifdef HAVE_BAUTH)
    return httpserver::webserver{cw};
}

void touch_request_accessors(const httpserver::http_request& req) {
    (void)req.get_user();              // was #ifdef HAVE_BAUTH
    (void)req.get_pass();              // was #ifdef HAVE_BAUTH
    (void)req.get_digested_user();     // was #ifdef HAVE_DAUTH
    (void)req.get_client_cert_dn();    // already unconditional (TASK-019)
    (void)req.has_tls_session();
    using ddr = httpserver::http::http_utils::digest_auth_result;
    (void)sizeof(ddr);

    // TASK-037: pin every remaining TLS cert accessor declared in
    // http_request.hpp. These were #ifdef HAVE_GNUTLS-gated before
    // TASK-019. Calling them on a TLS-off build returns the documented
    // sentinel (empty string / false / 0) without touching gnutls.
    (void)req.get_client_cert_issuer_dn();
    (void)req.get_client_cert_cn();
    (void)req.get_client_cert_fingerprint_sha256();
    (void)req.has_client_certificate();
    (void)req.is_client_cert_verified();
    (void)req.get_client_cert_not_before();
    (void)req.get_client_cert_not_after();

    // TASK-037: pin the DAUTH-gated check_digest_auth() declaration via
    // a member-function pointer rather than a call: taking the address
    // proves the symbol is declared unconditionally and resolves at link
    // time, without invoking a method that would throw on a build
    // without HAVE_DAUTH. Pinning *both* overloads (string-password and
    // pre-computed-digest) covers the full DAUTH surface that TASK-034
    // unconditionalised.
    using check_pw_t = httpserver::http::http_utils::digest_auth_result
        (httpserver::http_request::*)(
            const std::string&, const std::string&, unsigned int, uint32_t,
            httpserver::http::http_utils::digest_algorithm) const;
    check_pw_t cd_pw = &httpserver::http_request::check_digest_auth;
    (void)cd_pw;
    using check_digest_t = httpserver::http::http_utils::digest_auth_result
        (httpserver::http_request::*)(
            const std::string&, const void*, size_t, unsigned int, uint32_t,
            httpserver::http::http_utils::digest_algorithm) const;
    check_digest_t cd_dg = &httpserver::http_request::check_digest_auth_digest;
    (void)cd_dg;
}

// TASK-037: pin the *positive*-`true` form of the three feature-flag
// setters on create_webserver. Calling these with `true` on a build
// without the corresponding HAVE_* would, at runtime, refuse to set the
// flag or throw -- but the *compile* must succeed unconditionally, which
// is what the build-flag-invariance gate asserts. A member-function-
// pointer reference resolves at link time and proves the declaration is
// unconditional without forcing the runtime path.
void touch_create_webserver_setters() {
    using cw_setter = httpserver::create_webserver& (
        httpserver::create_webserver::*)(bool);
    cw_setter s_ssl = &httpserver::create_webserver::use_ssl;
    cw_setter s_bauth = &httpserver::create_webserver::basic_auth;
    cw_setter s_dauth = &httpserver::create_webserver::digest_auth;
    (void)s_ssl;
    (void)s_bauth;
    (void)s_dauth;
}

void touch_features() {
    auto f = httpserver::webserver::features();
    (void)f.basic_auth;
    (void)f.digest_auth;
    (void)f.tls;
    (void)f.websocket;
}

void touch_ws(httpserver::webserver& ws) {
    // TASK-035: exercise all three new public entry points. On
    // HAVE_WEBSOCKET-off builds each throws feature_unavailable; on
    // HAVE_WEBSOCKET-on builds the smart-ptr overloads throw
    // std::invalid_argument on the null inputs (and unregister_ws_resource
    // is a no-op on a missing path). Either way, the call sites must
    // compile, which is the whole point of this consumer-fixture TU.
    try {
        ws.register_ws_resource(
            "/ws", std::unique_ptr<httpserver::websocket_handler>{});
    } catch (...) {
    }
    try {
        ws.register_ws_resource(
            "/ws", std::shared_ptr<httpserver::websocket_handler>{});
    } catch (...) {
    }
    try {
        ws.unregister_ws_resource("/ws");
    } catch (...) {
    }
}

httpserver::http_request build_test_req() {
    return httpserver::create_test_request()
        .user("alice").pass("s3cret")          // was #ifdef HAVE_BAUTH
        .digested_user("alice")                 // was #ifdef HAVE_DAUTH
        .tls_enabled(true)                      // was #ifdef HAVE_GNUTLS
        .build();
}

}  // namespace fixture

int main() {
    (void)&fixture::build_server;
    (void)&fixture::touch_request_accessors;
    (void)&fixture::touch_features;
    (void)&fixture::touch_ws;
    (void)&fixture::build_test_req;
    (void)&fixture::touch_create_webserver_setters;  // TASK-037
    return 0;
}
