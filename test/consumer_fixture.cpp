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

// Compile-only consumer fixture: touches every public symbol whose declaration
// was previously guarded by #ifdef HAVE_* in the public headers.  This file
// MUST compile and link with the library in every combination of
// HAVE_BAUTH / HAVE_DAUTH / HAVE_GNUTLS / HAVE_WEBSOCKET.
//
// Runtime behaviour is irrelevant; main() returns 0 immediately.
// CI gate: .github/workflows/verify-build.yml.

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
    (void)req.get_client_cert_dn();    // already unconditional
    (void)req.has_tls_session();
    using ddr = httpserver::http::http_utils::digest_auth_result;
    (void)sizeof(ddr);

    // Pin every remaining TLS cert accessor declared in
    // http_request.hpp. These were #ifdef HAVE_GNUTLS-gated before being
    // made unconditional. Calling them on a TLS-off build returns the documented
    // sentinel (empty string / false / 0) without touching gnutls.
    (void)req.get_client_cert_issuer_dn();
    (void)req.get_client_cert_cn();
    (void)req.get_client_cert_fingerprint_sha256();
    (void)req.has_client_certificate();
    (void)req.is_client_cert_verified();
    (void)req.get_client_cert_not_before();
    (void)req.get_client_cert_not_after();

    // link-time proof that both check_digest_auth overloads are declared unconditionally.
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

// Prove use_ssl/basic_auth/digest_auth are declared unconditionally via
// member-function-pointer address-taking (no runtime invocation needed).
// Also touch each TLS credential-material setter with a no-op value so any
// future HAVE_GNUTLS guard regression is caught by the CI gate.
void touch_create_webserver_setters() {
    using cw_setter = httpserver::create_webserver& (
        httpserver::create_webserver::*)(bool);
    cw_setter s_ssl = &httpserver::create_webserver::use_ssl;
    cw_setter s_bauth = &httpserver::create_webserver::basic_auth;
    cw_setter s_dauth = &httpserver::create_webserver::digest_auth;
    (void)s_ssl;
    (void)s_bauth;
    (void)s_dauth;

    // TLS credential-material setters -- always unconditional.
    httpserver::create_webserver cw{8080};
    cw.raw_https_mem_key("");
    cw.raw_https_mem_cert("");
    cw.raw_https_mem_trust("");
    cw.https_priorities("");
    cw.https_mem_dhparams("");
    cw.cred_type(httpserver::http::http_utils::NONE);
    cw.psk_cred_handler([](const std::string&) { return std::string{}; });
    (void)cw;
}

void touch_features() {
    auto f = httpserver::webserver::features();
    (void)f.basic_auth;
    (void)f.digest_auth;
    (void)f.tls;
    (void)f.websocket;
}

void touch_ws(httpserver::webserver& ws) {
    // Exercise all three websocket-resource public entry points. On
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
    (void)&fixture::touch_create_webserver_setters;
    return 0;
}
