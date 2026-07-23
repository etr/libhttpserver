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

// Integration test: WWW-Authenticate challenge format on the wire.
//
// Spins up a webserver wired to digest_auth_random + nonce_nc_size,
// registers a resource that returns
// `http_response::unauthorized(digest_challenge{...})`, and asserts
// that the `WWW-Authenticate` response header issued on the first
// (no-Authorization) round contains the RFC 7616 §3.3-mandated
// parameters (Digest scheme, realm, nonce, opaque, algorithm, qop).
//
// We intentionally use plain `curl` (no `--digest`) here so the test
// observes the raw challenge byte stream rather than only the
// post-handshake 200 result — that's what the converted `digest_auth`
// test in authentication.cpp pins.

#if defined(_WIN32) && !defined(__CYGWIN__)
#define _WINDOWS
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x600
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_response;
using httpserver::http_resource;
using httpserver::http_request;
using httpserver::digest_challenge;
using httpserver::http::http_utils;

namespace {

size_t writefunc(void* ptr, size_t size, size_t nmemb, std::string* s) {
    s->append(reinterpret_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Case-insensitive substring search (HTTP header tokens are ASCII).
bool ci_contains(const std::string& haystack, const std::string& needle) {
    return std::search(haystack.begin(), haystack.end(),
                        needle.begin(), needle.end(),
                        [](unsigned char a, unsigned char b) {
                            return std::tolower(a) == std::tolower(b);
                        }) != haystack.end();
}

#ifdef HAVE_DAUTH
class challenge_resource : public http_resource {
 public:
    http_response render_get(const http_request& /*req*/) override {
        return http_response::unauthorized(
            digest_challenge{.realm = "testrealm@host.com",
                             .response_body  = "access denied"});
    }
};

// Resource that emits a SHA-256 Digest challenge (RFC 7616 §3.3 algorithm
// coverage — wire-observable, unlike the unit-test friend-hook assertions).
class sha256_challenge_resource : public http_resource {
 public:
    http_response render_get(const http_request& /*req*/) override {
        return http_response::unauthorized(
            digest_challenge{.realm      = "sha256realm@host.com",
                             .algorithm  = http_utils::digest_algorithm::SHA256,
                             .response_body       = "denied"});
    }
};

// Resource that emits a challenge with an explicit opaque and domain so we
// can verify those fields appear in the wire challenge (RFC 7616 §3.3).
class opaque_domain_challenge_resource : public http_resource {
 public:
    http_response render_get(const http_request& /*req*/) override {
        return http_response::unauthorized(
            digest_challenge{.realm   = "odrealm@host.com",
                             .opaque  = "deadbeef",
                             .domain  = "/protected",
                             .response_body    = "denied"});
    }
};
#endif  // HAVE_DAUTH

}  // namespace

LT_BEGIN_SUITE(digest_challenge_format_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(digest_challenge_format_suite)

#ifdef HAVE_DAUTH

LT_BEGIN_AUTO_TEST(digest_challenge_format_suite,
                   rfc7616_challenge_carries_required_fields)
    webserver ws{create_webserver(0)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto resource = std::make_shared<challenge_resource>();
    ws.register_path("guarded", resource);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string guarded_url =
        "localhost:" + std::to_string(port) + "/guarded";

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string body;
    std::string headers;
    CURL* curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_URL, guarded_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 401);
    LT_CHECK_EQ(body, std::string("access denied"));

    // RFC 7616 §3.3 mandates every Digest challenge include realm,
    // nonce, qop, and algorithm. opaque is RECOMMENDED but in
    // libhttpserver we always emit it. Assert each token appears in
    // the WWW-Authenticate header (case-insensitive).
    LT_CHECK_EQ(ci_contains(headers, "WWW-Authenticate"), true);
    LT_CHECK_EQ(ci_contains(headers, "Digest"), true);
    LT_CHECK_EQ(ci_contains(headers, "realm=\"testrealm@host.com\""), true);
    LT_CHECK_EQ(ci_contains(headers, "nonce="), true);
    LT_CHECK_EQ(ci_contains(headers, "opaque="), true);
    LT_CHECK_EQ(ci_contains(headers, "algorithm="), true);
    LT_CHECK_EQ(ci_contains(headers, "qop="), true);

    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(rfc7616_challenge_carries_required_fields)

// RFC 7616 §3.3 algorithm coverage: when digest_challenge.algorithm is set
// to SHA256 the WWW-Authenticate header on the wire must contain
// "SHA-256" (the canonical token per RFC 7616 §3.3, distinct from MD5).
// This is a wire-format assertion -- the only reliable place to verify it,
// since the dispatch path (MHD_queue_auth_required_response3) owns the
// algorithm token serialisation.
LT_BEGIN_AUTO_TEST(digest_challenge_format_suite,
                   rfc7616_challenge_sha256_algorithm_in_header)
    webserver ws{create_webserver(0)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto resource = std::make_shared<sha256_challenge_resource>();
    ws.register_path("sha256", resource);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string sha256_url =
        "localhost:" + std::to_string(port) + "/sha256";

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string body;
    std::string headers;
    CURL* curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_URL, sha256_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 401);
    LT_CHECK_EQ(body, std::string("denied"));
    // RFC 7616 §3.3: the algorithm token for SHA-256 is "SHA-256".
    LT_CHECK_EQ(ci_contains(headers, "SHA-256"), true);

    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(rfc7616_challenge_sha256_algorithm_in_header)

// Wire-format round-trip for the opaque and domain fields.  The unit tests
// formerly asserted these through the internal get_params() friend hook;
// wire-format assertions here are the appropriate place.
LT_BEGIN_AUTO_TEST(digest_challenge_format_suite,
                   rfc7616_challenge_opaque_and_domain_in_header)
    webserver ws{create_webserver(0)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto resource = std::make_shared<opaque_domain_challenge_resource>();
    ws.register_path("od", resource);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string od_url =
        "localhost:" + std::to_string(port) + "/od";

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string body;
    std::string headers;
    CURL* curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_URL, od_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 401);
    // The opaque and domain fields must appear as RFC 7616 §3.3
    // quoted-string auth-params, not merely as bare substrings anywhere in
    // the header (a bare-substring check would also pass if the value
    // leaked into e.g. realm= or nonce= by accident).
    LT_CHECK_EQ(ci_contains(headers, "opaque=\"deadbeef\""), true);
    LT_CHECK_EQ(ci_contains(headers, "domain=\"/protected\""), true);

    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(rfc7616_challenge_opaque_and_domain_in_header)

#else  // !HAVE_DAUTH

LT_BEGIN_AUTO_TEST(digest_challenge_format_suite, dauth_disabled_noop)
    LT_CHECK_EQ(true, true);
LT_END_AUTO_TEST(dauth_disabled_noop)

#endif  // HAVE_DAUTH

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
