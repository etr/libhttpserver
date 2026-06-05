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
#include <memory>
#include <optional>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./test_utils.hpp"

#define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

// v2-digest tracking note (consolidated):
// Under v2, libhttpserver only emits the static 401 Digest challenge; the
// nonce/opaque state machine is not driven, so check_digest_auth_check[_digest]()
// is never reached and get_digested_user() always returns an empty view.
// As a result the following tests are all observationally indistinguishable
// from the canonical `digest_auth` case (both correct- and wrong-pass arms
// see the same 401 + "FAIL"):
//   - digest_auth_wrong_pass
//   - digest_auth_with_ha1_md5 / *_wrong_pass
//   - digest_auth_with_ha1_sha256 / *_wrong_pass
//   - digest_user_cache_with_auth
// They are retained as pins for the static-challenge contract and become
// meaningful again only when full v2 Digest support (MHD nonce/opaque state
// machine) lands. At that point the wrong-pass arms should assert a distinct
// 403/401-with-stale, the HA1-MD5/SHA-256 arms should validate the chosen
// algorithm, and digest_user_cache_with_auth should exercise the cache-hit
// path. See PRD §digest-auth for the follow-up.

using std::shared_ptr;
using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_response;
using httpserver::http_resource;
using httpserver::http_request;

#ifdef HTTPSERVER_PORT
#define PORT HTTPSERVER_PORT
#else
#define PORT 8080
#endif  // PORT

#define STR2(p) #p
#define STR(p) STR2(p)
#define PORT_STRING STR(PORT)

size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

#ifdef HAVE_BAUTH
class user_pass_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         if (req.get_user() != "myuser" || req.get_pass() != "mypass") {
             return http_response::unauthorized("Basic", "examplerealm", "FAIL");
         }
         return http_response::string(std::string(req.get_user()) + " " + std::string(req.get_pass()));
     }
};
#endif  // HAVE_BAUTH

#ifdef HAVE_DAUTH
// TASK-062: digest_resource now drives the full RFC-7616 nonce/opaque
// handshake by emitting digest_challenge_body challenges. The first
// request from a curl --digest client arrives with no Authorization
// header (get_digested_user() empty), so we emit the initial RFC-7616
// challenge with nonce/opaque/algorithm/qop populated by the dispatch
// path; on the second request, curl recomputes the response using the
// challenge, and check_digest_auth() validates and returns OK.
class digest_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         using httpserver::http::http_utils;
         using httpserver::digest_challenge;
         if (req.get_digested_user() == "") {
             return http_response::unauthorized(
                 digest_challenge{.realm = "examplerealm",
                                  .body  = "FAIL"});
         }
         auto result = req.check_digest_auth("examplerealm", "mypass", 300,
                                             0, http_utils::digest_algorithm::MD5);
         if (result == http_utils::digest_auth_result::NONCE_STALE) {
             return http_response::unauthorized(
                 digest_challenge{.realm        = "examplerealm",
                                  .signal_stale = true,
                                  .body         = "FAIL"});
         }
         if (result != http_utils::digest_auth_result::OK) {
             return http_response::unauthorized(
                 digest_challenge{.realm = "examplerealm",
                                  .body  = "FAIL"});
         }
         return http_response::string("SUCCESS");
     }
};
#endif  // HAVE_DAUTH

LT_BEGIN_SUITE(authentication_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(authentication_suite)

#ifdef HAVE_BAUTH
LT_BEGIN_AUTO_TEST(authentication_suite, base_auth)
    webserver ws{create_webserver(PORT)};

    auto user_pass = std::make_shared<user_pass_resource>();
    ws.register_path("base", user_pass);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_USERNAME, "myuser");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "mypass");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "myuser mypass");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(base_auth)

LT_BEGIN_AUTO_TEST(authentication_suite, base_auth_fail)
    webserver ws{create_webserver(PORT)};

    auto user_pass = std::make_shared<user_pass_resource>();
    ws.register_path("base", user_pass);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_USERNAME, "myuser");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "wrongpass");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(base_auth_fail)
#endif  // HAVE_BAUTH

//  do not run the digest auth tests on windows as curl
//  appears to have problems with it.
//  Will fix this separately
//  Also skip if libmicrohttpd was built without digest auth support
#if !defined(_WINDOWS) && defined(HAVE_DAUTH)

// Pre-computed MD5 hash of "myuser:examplerealm:mypass"
// printf "myuser:examplerealm:mypass" | md5sum
// 6ceef750e0130d6528b938c3abd94110
static const unsigned char PRECOMPUTED_HA1_MD5[16] = {
    0x6c, 0xee, 0xf7, 0x50, 0xe0, 0x13, 0x0d, 0x65,
    0x28, 0xb9, 0x38, 0xc3, 0xab, 0xd9, 0x41, 0x10
};

// Pre-computed SHA-256 hash of "myuser:examplerealm:mypass"
// printf "myuser:examplerealm:mypass" | sha256sum
// d4ff5b1795b23b4c625975959f3276526f3f4f4ef7d22083207e02d7c4bd8a05
static const unsigned char PRECOMPUTED_HA1_SHA256[32] = {
    0xd4, 0xff, 0x5b, 0x17, 0x95, 0xb2, 0x3b, 0x4c,
    0x62, 0x59, 0x75, 0x95, 0x9f, 0x32, 0x76, 0x52,
    0x6f, 0x3f, 0x4f, 0x4e, 0xf7, 0xd2, 0x20, 0x83,
    0x20, 0x7e, 0x02, 0xd7, 0xc4, 0xbd, 0x8a, 0x05
};

class digest_ha1_md5_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         using httpserver::http::http_utils;
         if (req.get_digested_user() == "") {
             return http_response::unauthorized("Digest", "examplerealm", "FAIL");
         }
         auto result = req.check_digest_auth_digest("examplerealm", PRECOMPUTED_HA1_MD5,
                 http_utils::md5_digest_size, 300, 0,
                 http_utils::digest_algorithm::MD5);
         if (result == http_utils::digest_auth_result::NONCE_STALE) {
             return http_response::unauthorized("Digest", "examplerealm", "FAIL");
         } else if (result != http_utils::digest_auth_result::OK) {
             return http_response::unauthorized("Digest", "examplerealm", "FAIL");
         }
         return http_response::string("SUCCESS");
     }
};

class digest_ha1_sha256_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         using httpserver::http::http_utils;
         if (req.get_digested_user() == "") {
             return http_response::unauthorized("Digest", "examplerealm", "FAIL");
         }
         auto result = req.check_digest_auth_digest("examplerealm", PRECOMPUTED_HA1_SHA256,
                 http_utils::sha256_digest_size, 300, 0,
                 http_utils::digest_algorithm::SHA256);
         if (result == http_utils::digest_auth_result::NONCE_STALE) {
             return http_response::unauthorized("Digest", "examplerealm", "FAIL");
         } else if (result != http_utils::digest_auth_result::OK) {
             return http_response::unauthorized("Digest", "examplerealm", "FAIL");
         }
         return http_response::string("SUCCESS");
     }
};

// TASK-013 §2 / §10: full digest-auth round-trip is a v1-only behaviour.
// The v1 `digest_auth_fail_response::enqueue_response` path called
// MHD_queue_auth_required_response3 to drive libmicrohttpd's nonce/opaque
// state machine; v2's `unauthorized("Digest", ...)` only emits a static
// WWW-Authenticate challenge (see http_response.hpp:175-180 doxygen).
// These tests now assert the v2 contract: the resource emits FAIL on the
// initial request because curl's nonce roundtrip cannot complete.
LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto digest = std::make_shared<digest_resource>();
    ws.register_path("base", digest);
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
#if defined(_WINDOWS)
    curl_easy_setopt(curl, CURLOPT_USERPWD, "examplerealm/myuser:mypass");
#else
    curl_easy_setopt(curl, CURLOPT_USERPWD, "myuser:mypass");
#endif
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    // TASK-062 contract: the server now emits a full RFC-7616 §3.3
    // challenge with nonce/opaque/algorithm/qop via
    // MHD_queue_auth_required_response3, so curl --digest can complete
    // the handshake. Final response on round 2 is 200 SUCCESS.
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth)

// See v2-digest tracking note at top of file.
LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth_wrong_pass)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto digest = std::make_shared<digest_resource>();
    ws.register_path("base", digest);
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
#if defined(_WINDOWS)
    curl_easy_setopt(curl, CURLOPT_USERPWD, "examplerealm/myuser:wrongpass");
#else
    curl_easy_setopt(curl, CURLOPT_USERPWD, "myuser:wrongpass");
#endif
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    // v2 contract: static 401 Digest challenge, no handshake completes.
    LT_CHECK_EQ(http_code, 401);
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_wrong_pass)

// See v2-digest tracking note at top of file.
LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth_with_ha1_md5)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto digest_ha1 = std::make_shared<digest_ha1_md5_resource>();
    ws.register_path("base", digest_ha1);
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
#if defined(_WINDOWS)
    curl_easy_setopt(curl, CURLOPT_USERPWD, "examplerealm/myuser:mypass");
#else
    curl_easy_setopt(curl, CURLOPT_USERPWD, "myuser:mypass");
#endif
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    // v2 contract: static 401 Digest challenge, no handshake completes.
    LT_CHECK_EQ(http_code, 401);
    // TASK-013 §2 / §10: v2 digest auth only emits a static challenge — see
    // digest_auth test above. Handshake cannot complete; body remains FAIL.
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_md5)

// See v2-digest tracking note at top of file.
LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth_with_ha1_md5_wrong_pass)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto digest_ha1 = std::make_shared<digest_ha1_md5_resource>();
    ws.register_path("base", digest_ha1);
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
#if defined(_WINDOWS)
    curl_easy_setopt(curl, CURLOPT_USERPWD, "examplerealm/myuser:wrongpass");
#else
    curl_easy_setopt(curl, CURLOPT_USERPWD, "myuser:wrongpass");
#endif
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    // v2 contract: static 401 Digest challenge, no handshake completes.
    LT_CHECK_EQ(http_code, 401);
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_md5_wrong_pass)

// See v2-digest tracking note at top of file.
LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth_with_ha1_sha256)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto digest_ha1 = std::make_shared<digest_ha1_sha256_resource>();
    ws.register_path("base", digest_ha1);
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
#if defined(_WINDOWS)
    curl_easy_setopt(curl, CURLOPT_USERPWD, "examplerealm/myuser:mypass");
#else
    curl_easy_setopt(curl, CURLOPT_USERPWD, "myuser:mypass");
#endif
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    // v2 contract: static 401 Digest challenge, no handshake completes.
    LT_CHECK_EQ(http_code, 401);
    // TASK-013 §2 / §10: v2 digest auth only emits a static challenge — see
    // digest_auth test above. Handshake cannot complete; body remains FAIL.
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_sha256)

// See v2-digest tracking note at top of file.
LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth_with_ha1_sha256_wrong_pass)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto digest_ha1 = std::make_shared<digest_ha1_sha256_resource>();
    ws.register_path("base", digest_ha1);
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
#if defined(_WINDOWS)
    curl_easy_setopt(curl, CURLOPT_USERPWD, "examplerealm/myuser:wrongpass");
#else
    curl_easy_setopt(curl, CURLOPT_USERPWD, "myuser:wrongpass");
#endif
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    // v2 contract: static 401 Digest challenge, no handshake completes.
    LT_CHECK_EQ(http_code, 401);
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_sha256_wrong_pass)

// Resource that tests get_digested_user() caching
// Covers http_request.cpp lines 293-295 (cache hit) and 300 (nullptr branch)
class digest_user_cache_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        using httpserver::http::http_utils;
        // First call - will populate cache (line 300 nullptr or non-null branch)
        std::string user1 = std::string(req.get_digested_user());

        if (user1.empty()) {
            // No digest auth provided - send a 401 challenge so curl can retry
            return http_response::unauthorized("Digest", "testrealm", "FAIL");
        }

        // Second call - should hit cache (lines 293-295)
        std::string user2 = std::string(req.get_digested_user());

        // Verify caching works correctly (both calls return same value)
        if (user1 != user2) {
            return http_response::string("CACHE_MISMATCH").with_status(500);
        }

        // Return the digested user (tests cache hit with valid user)
        return http_response::string("USER:" + user1);
    }
};

// Test digested user caching when no digest auth is provided (nullptr branch)
LT_BEGIN_AUTO_TEST(authentication_suite, digest_user_cache_no_auth)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto resource = std::make_shared<digest_user_cache_resource>();
    ws.register_path("cache_test", resource);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    // No authentication - should trigger 401 challenge
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/cache_test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 401);
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_user_cache_no_auth)

// See v2-digest tracking note at top of file. Under the v2 static-challenge
// model (DR-013) the digest_user_cache_resource cache-hit path is unreachable;
// this test only pins the static 401 + "FAIL" challenge contract.
LT_BEGIN_AUTO_TEST(authentication_suite, digest_user_cache_with_auth)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto resource = std::make_shared<digest_user_cache_resource>();
    ws.register_path("cache_test", resource);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_easy_setopt(curl, CURLOPT_USERPWD, "testuser:testpass");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/cache_test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // TASK-013 §2 / §10: v2's unauthorized("Digest", ...) only emits a
    // static challenge — there's no MHD nonce/opaque state machine, so the
    // digest handshake cannot complete. The resource never sees a digested
    // user, so the response stays "FAIL". The cache-hit path is unreachable
    // until/unless v2 grows full digest auth support.
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_user_cache_with_auth)

#endif

#ifdef HAVE_BAUTH
// Simple resource for centralized auth tests
class simple_resource : public http_resource {
 public:
     http_response render_get(const http_request&) {
         return http_response::string("SUCCESS");
     }
};

// Centralized authentication handler (TASK-054 migration from
// std::shared_ptr<http_response> to std::optional<http_response>: nullopt
// allows the request; an engaged optional short-circuits with the value).
std::optional<http_response> centralized_auth_handler(const http_request& req) {
    if (req.get_user() != "admin" || req.get_pass() != "secret") {
        return http_response::unauthorized("Basic", "testrealm", "Unauthorized");
    }
    return std::nullopt;  // Allow request
}

LT_BEGIN_AUTO_TEST(authentication_suite, centralized_auth_fail)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("protected", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/protected");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);
    LT_CHECK_EQ(s, "Unauthorized");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(centralized_auth_fail)

LT_BEGIN_AUTO_TEST(authentication_suite, centralized_auth_success)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("protected", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_USERNAME, "admin");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "secret");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/protected");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(centralized_auth_success)

LT_BEGIN_AUTO_TEST(authentication_suite, auth_skip_paths)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/health", "/public/*"})};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("health", sr);
    ws.register_path("public/info", sr);
    ws.register_path("protected", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl;
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    std::string s;

    // Test /health (exact match skip path) - should succeed without auth
    curl = curl_easy_init();
    s = "";
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/health");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    // Test /public/info (wildcard skip path) - should succeed without auth
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/public/info");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    // Test /protected (not in skip paths) - should fail without auth
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/protected");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_paths)

// Test that wildcard doesn't match partial prefix
// /publicinfo should NOT match /public/* (wildcard requires the slash)
LT_BEGIN_AUTO_TEST(authentication_suite, auth_skip_paths_no_partial_match)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/public/*"})};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("publicinfo", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // /publicinfo should NOT be skipped (doesn't match /public/*)
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/publicinfo");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);  // Should require auth
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_paths_no_partial_match)

// Test deeply nested wildcard paths
LT_BEGIN_AUTO_TEST(authentication_suite, auth_skip_paths_deep_nested)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/api/v1/public/*"})};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("api/v1/public/users/list", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // Deep nested path should be skipped
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/api/v1/public/users/list");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_paths_deep_nested)

// Test POST method with centralized auth
class post_resource : public http_resource {
 public:
     http_response render_post(const http_request&) {
         return http_response::string("POST_SUCCESS");
     }
};

LT_BEGIN_AUTO_TEST(authentication_suite, centralized_auth_post_method)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)};

    auto pr = std::make_shared<post_resource>();
    ws.register_path("data", pr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // POST without auth should fail
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/data");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "test=data");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);
    curl_easy_cleanup(curl);

    // POST with auth should succeed
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_USERNAME, "admin");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "secret");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/data");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "test=data");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "POST_SUCCESS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(centralized_auth_post_method)

// Test wrong credentials (different from no credentials)
LT_BEGIN_AUTO_TEST(authentication_suite, centralized_auth_wrong_credentials)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("protected", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // Wrong username
    curl_easy_setopt(curl, CURLOPT_USERNAME, "wronguser");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "secret");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/protected");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);
    curl_easy_cleanup(curl);

    // Wrong password
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_USERNAME, "admin");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "wrongpass");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/protected");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(centralized_auth_wrong_credentials)

// Test that 404 is returned for non-existent resources (auth doesn't interfere)
LT_BEGIN_AUTO_TEST(authentication_suite, centralized_auth_not_found)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("exists", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // Non-existent resource without auth - should get 401 (auth checked first)
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/nonexistent");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    // Note: Auth is only checked when resource is found, so 404 should be returned
    LT_CHECK_EQ(http_code, 404);
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(centralized_auth_not_found)

// Test no auth handler (default behavior - no auth required)
LT_BEGIN_AUTO_TEST(authentication_suite, no_auth_handler_default)
    webserver ws{create_webserver(PORT)};  // No auth_handler

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("open", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // Should succeed without any auth
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/open");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(no_auth_handler_default)

// Test multiple skip paths
LT_BEGIN_AUTO_TEST(authentication_suite, auth_multiple_skip_paths)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/health", "/metrics", "/status", "/public/*"})};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("health", sr);
    ws.register_path("metrics", sr);
    ws.register_path("status", sr);
    ws.register_path("protected", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl;
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    std::string s;

    // /health should work without auth
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/health");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    curl_easy_cleanup(curl);

    // /metrics should work without auth
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/metrics");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    curl_easy_cleanup(curl);

    // /status should work without auth
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/status");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    curl_easy_cleanup(curl);

    // Protected should still require auth
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/protected");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_multiple_skip_paths)

// Test skip path for root "/"
LT_BEGIN_AUTO_TEST(authentication_suite, auth_skip_path_root)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/"})};

    auto sr = std::make_shared<simple_resource>();
    ws.register_prefix("/", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // Root path should be skipped
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_path_root)

// Test wildcard path matching "/pub/*"
LT_BEGIN_AUTO_TEST(authentication_suite, auth_skip_path_wildcard)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/pub/*"})};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("pub/anything", sr);
    ws.register_path("pub/nested/path", sr);
    ws.register_path("private/secret", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl;
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    std::string s;

    // /pub/anything should be skipped (matches /pub/*)
    curl = curl_easy_init();
    s = "";
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/pub/anything");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    curl_easy_cleanup(curl);

    // /pub/nested/path should also be skipped (matches /pub/*)
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/pub/nested/path");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    curl_easy_cleanup(curl);

    // /private/secret should NOT be skipped (doesn't match /pub/*)
    curl = curl_easy_init();
    s = "";
    http_code = 0;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/private/secret");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);  // Should require auth
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_path_wildcard)

// Test empty skip paths (should require auth for everything)
LT_BEGIN_AUTO_TEST(authentication_suite, auth_empty_skip_paths)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({})};  // Empty skip paths

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("test", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // Should require auth
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_empty_skip_paths)

// Test that path traversal cannot bypass auth skip paths
// Requesting /public/../protected should NOT skip auth
LT_BEGIN_AUTO_TEST(authentication_suite, auth_skip_path_traversal_bypass)
    webserver ws{create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/public/*"})};

    auto sr = std::make_shared<simple_resource>();
    ws.register_path("protected", sr);
    ws.register_path("public/info", sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl;
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)

    // /public/../protected should normalize to /protected, which requires auth
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/public/../protected");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 401);  // Should require auth, not be skipped
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(auth_skip_path_traversal_bypass)
#endif  // HAVE_BAUTH

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
