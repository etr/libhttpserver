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
#include "./digest_client.hpp"

#define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

// v2-digest tracking note (updated after TASK-079):
//
// After TASK-062 the resources emit full RFC-7616 challenges via
// http_response::unauthorized(digest_challenge{...}). After TASK-079 the
// tests drive the nonce/opaque/qop state machine themselves rather than
// delegating to libcurl's CURLAUTH_DIGEST automation, and every test below
// performs an explicit two-round flow:
//   round 1: plain GET, expect 401 + RFC 7616 §3.3 challenge
//   round 2: GET with hand-built `Authorization: Digest …` header
//
// This pins behaviours that libcurl previously masked:
//   - For *_wrong_pass: the 401 fires on the second request (not just on
//     the initial challenge), proving check_digest_auth* rejected the
//     response token rather than the wire being short-circuited earlier.
//   - For *_ha1_*: the test signs with a precomputed HA1 directly and
//     never sends cleartext over the wire. The server returns 200 only if
//     it validates against the configured HA1 (not by re-deriving
//     MD5/SHA-256 from a password it never received). The wrong-HA1
//     negative variants strengthen the proof: signing with an HA1 derived
//     from a *different* password produces 401, even though the server is
//     configured with the original HA1.
//   - digest_user_cache_with_auth now reaches the cache-hit code path
//     (digest_user_cache_resource was migrated to digest_challenge in
//     TASK-079 so the handshake can complete).
//
// Remaining gap (signal_stale re-challenge path):
//   The NONCE_STALE branch inside digest_resource/digest_ha1_*_resource
//   (signal_stale = true) cannot be reliably triggered in CI because MHD's
//   nonce expiry requires a real time delay and concurrent replay
//   requests. This gap is documented in TASK-062 test-quality-reviewer
//   iter1-2; it will be covered when full nonce-expiry testing
//   infrastructure lands.

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

// TASK-079: CURLOPT_HEADERFUNCTION-shaped sink so the test can capture the
// raw `WWW-Authenticate` line from the round-1 401 response and feed it to
// httpserver_test::extract_digest_challenge().
size_t headerfunc(void *ptr, size_t size, size_t nmemb, std::string *s) {
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

// MinGW64's curl --digest parser does not accept the challenge produced by
// MHD's MHD_queue_auth_required_response3 on Windows, so the authenticated
// round-trip never completes. Predates TASK-062 (RFC-7616) and is a
// test-infrastructure issue, not a digest-algorithm issue. The HAVE_DAUTH
// guard separately skips the block when libmicrohttpd is built without
// digest-auth support.
// reason: see test/PORTABILITY.md §authentication.cpp digest-auth block.
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

// TASK-062: migrated to digest_challenge so the nonce/opaque handshake can
// complete and check_digest_auth_digest() is exercised. Previously used the
// legacy string overload which emitted a static challenge with no nonce,
// making the handshake impossible and the algorithm parameter meaningless.
class digest_ha1_md5_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         using httpserver::http::http_utils;
         using httpserver::digest_challenge;
         if (req.get_digested_user() == "") {
             return http_response::unauthorized(
                 digest_challenge{.realm     = "examplerealm",
                                  .algorithm = http_utils::digest_algorithm::MD5,
                                  .body      = "FAIL"});
         }
         auto result = req.check_digest_auth_digest("examplerealm", PRECOMPUTED_HA1_MD5,
                 http_utils::md5_digest_size, 300, 0,
                 http_utils::digest_algorithm::MD5);
         if (result == http_utils::digest_auth_result::NONCE_STALE) {
             return http_response::unauthorized(
                 digest_challenge{.realm        = "examplerealm",
                                  .algorithm    = http_utils::digest_algorithm::MD5,
                                  .signal_stale = true,
                                  .body         = "FAIL"});
         } else if (result != http_utils::digest_auth_result::OK) {
             return http_response::unauthorized(
                 digest_challenge{.realm     = "examplerealm",
                                  .algorithm = http_utils::digest_algorithm::MD5,
                                  .body      = "FAIL"});
         }
         return http_response::string("SUCCESS");
     }
};

// TASK-062: migrated to digest_challenge with algorithm=SHA256 so the
// nonce/opaque handshake can complete and check_digest_auth_digest() is
// exercised with the SHA-256 algorithm. Previously used the legacy string
// overload which made the algorithm parameter meaningless.
class digest_ha1_sha256_resource : public http_resource {
 public:
     http_response render_get(const http_request& req) {
         using httpserver::http::http_utils;
         using httpserver::digest_challenge;
         if (req.get_digested_user() == "") {
             return http_response::unauthorized(
                 digest_challenge{.realm     = "examplerealm",
                                  .algorithm = http_utils::digest_algorithm::SHA256,
                                  .body      = "FAIL"});
         }
         auto result = req.check_digest_auth_digest("examplerealm", PRECOMPUTED_HA1_SHA256,
                 http_utils::sha256_digest_size, 300, 0,
                 http_utils::digest_algorithm::SHA256);
         if (result == http_utils::digest_auth_result::NONCE_STALE) {
             return http_response::unauthorized(
                 digest_challenge{.realm        = "examplerealm",
                                  .algorithm    = http_utils::digest_algorithm::SHA256,
                                  .signal_stale = true,
                                  .body         = "FAIL"});
         } else if (result != http_utils::digest_auth_result::OK) {
             return http_response::unauthorized(
                 digest_challenge{.realm     = "examplerealm",
                                  .algorithm = http_utils::digest_algorithm::SHA256,
                                  .body      = "FAIL"});
         }
         return http_response::string("SUCCESS");
     }
};

// TASK-079: two-round hand-rolled RFC 7616 §3.4 flow. Round 1 fetches the
// challenge, the test parses it and computes the response client-side,
// round 2 ships an explicit `Authorization: Digest …` header. Asserts the
// full handshake terminates in 200 SUCCESS.
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

    // Round 1: plain GET, capture the WWW-Authenticate challenge.
    std::string body1;
    std::string headers1;
    long http_code1 = 0;  // NOLINT(runtime/int)
    CURL *curl1 = curl_easy_init();
    curl_easy_setopt(curl1, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl1, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl1, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl1, CURLOPT_WRITEDATA, &body1);
    curl_easy_setopt(curl1, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl1, CURLOPT_HEADERDATA, &headers1);
    curl_easy_setopt(curl1, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl1, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl1), 0);
    curl_easy_getinfo(curl1, CURLINFO_RESPONSE_CODE, &http_code1);
    LT_CHECK_EQ(http_code1, 401);
    curl_easy_cleanup(curl1);

    auto challenge = httpserver_test::extract_digest_challenge(headers1);
    LT_ASSERT_EQ(challenge.has_value(), true);
    LT_CHECK_EQ(challenge->qop, std::string("auth"));
    // digest_resource emits algorithm=MD5 implicitly (default).

    // Round 2: hand-built Authorization header carrying our response.
    std::string cnonce = httpserver_test::make_cnonce();
    std::string response = httpserver_test::compute_response_cleartext(
        *challenge, httpserver_test::digest_hash::md5,
        "GET", "/base", "myuser", "mypass", cnonce, "00000001");
    std::string auth_header_value = httpserver_test::build_authorization_header(
        *challenge, "myuser", "/base", cnonce, "00000001", response);
    std::string auth_header = "Authorization: " + auth_header_value;

    std::string body2;
    long http_code2 = 0;  // NOLINT(runtime/int)
    CURL *curl2 = curl_easy_init();
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_header.c_str());
    curl_easy_setopt(curl2, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl2, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &body2);
    curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl2, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl2), 0);
    curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code2);
    LT_CHECK_EQ(http_code2, 200);
    LT_CHECK_EQ(body2, std::string("SUCCESS"));
    curl_easy_cleanup(curl2);
    curl_slist_free_all(hdrs);

    ws.stop();
LT_END_AUTO_TEST(digest_auth)

// TASK-079: wrong-password variant. The 401 fires on round 2 (after the
// client has computed a response with the wrong cleartext) -- under the
// pre-TASK-079 libcurl-driven shape the round-1 challenge alone could
// satisfy the body=="FAIL" assertion, but here we explicitly assert the
// rejection happens after the full handshake completes.
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

    // Round 1.
    std::string body1, headers1;
    long http_code1 = 0;  // NOLINT(runtime/int)
    CURL *curl1 = curl_easy_init();
    curl_easy_setopt(curl1, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl1, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl1, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl1, CURLOPT_WRITEDATA, &body1);
    curl_easy_setopt(curl1, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl1, CURLOPT_HEADERDATA, &headers1);
    curl_easy_setopt(curl1, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl1, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl1), 0);
    curl_easy_getinfo(curl1, CURLINFO_RESPONSE_CODE, &http_code1);
    LT_CHECK_EQ(http_code1, 401);
    curl_easy_cleanup(curl1);

    auto challenge = httpserver_test::extract_digest_challenge(headers1);
    LT_ASSERT_EQ(challenge.has_value(), true);

    // Round 2 -- sign with wrong password.
    std::string cnonce = httpserver_test::make_cnonce();
    std::string response = httpserver_test::compute_response_cleartext(
        *challenge, httpserver_test::digest_hash::md5,
        "GET", "/base", "myuser", "wrongpass", cnonce, "00000001");
    std::string auth_header = "Authorization: " +
        httpserver_test::build_authorization_header(
            *challenge, "myuser", "/base", cnonce, "00000001", response);

    std::string body2;
    long http_code2 = 0;  // NOLINT(runtime/int)
    CURL *curl2 = curl_easy_init();
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_header.c_str());
    curl_easy_setopt(curl2, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl2, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &body2);
    curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl2, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl2), 0);
    curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code2);
    // The 401 here is the round-2 server-side rejection: check_digest_auth()
    // returned NOT_AUTHORIZED because the response token didn't match what
    // the server computed from the configured password "mypass".
    LT_CHECK_EQ(http_code2, 401);
    LT_CHECK_EQ(body2, std::string("FAIL"));
    curl_easy_cleanup(curl2);
    curl_slist_free_all(hdrs);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_wrong_pass)

// TASK-079: HA1-precomputed MD5 path. The test never sends cleartext over
// the wire and never feeds CURLOPT_USERPWD: round 2 signs with the same
// 16-byte HA1 the server is configured with. A successful 200 proves the
// server is verifying against the configured HA1 (not by recomputing
// MD5("myuser:examplerealm:mypass") from cleartext it never received).
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

    // Round 1.
    std::string body1, headers1;
    long http_code1 = 0;  // NOLINT(runtime/int)
    CURL *curl1 = curl_easy_init();
    curl_easy_setopt(curl1, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl1, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl1, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl1, CURLOPT_WRITEDATA, &body1);
    curl_easy_setopt(curl1, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl1, CURLOPT_HEADERDATA, &headers1);
    curl_easy_setopt(curl1, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl1, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl1), 0);
    curl_easy_getinfo(curl1, CURLINFO_RESPONSE_CODE, &http_code1);
    LT_CHECK_EQ(http_code1, 401);
    curl_easy_cleanup(curl1);

    auto challenge = httpserver_test::extract_digest_challenge(headers1);
    LT_ASSERT_EQ(challenge.has_value(), true);
    // The resource emits algorithm=MD5; assert it reached the wire.
    LT_CHECK_EQ(challenge->algorithm, std::string("MD5"));

    // Round 2 -- sign with the precomputed HA1 directly.
    std::string cnonce = httpserver_test::make_cnonce();
    std::string response = httpserver_test::compute_response_ha1(
        *challenge, httpserver_test::digest_hash::md5,
        "GET", "/base", PRECOMPUTED_HA1_MD5, 16, cnonce, "00000001");
    std::string auth_header = "Authorization: " +
        httpserver_test::build_authorization_header(
            *challenge, "myuser", "/base", cnonce, "00000001", response);

    std::string body2;
    long http_code2 = 0;  // NOLINT(runtime/int)
    CURL *curl2 = curl_easy_init();
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_header.c_str());
    curl_easy_setopt(curl2, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl2, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &body2);
    curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl2, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl2), 0);
    curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code2);
    LT_CHECK_EQ(http_code2, 200);
    LT_CHECK_EQ(body2, std::string("SUCCESS"));
    curl_easy_cleanup(curl2);
    curl_slist_free_all(hdrs);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_md5)

// TASK-079: HA1-precomputed MD5 negative variant. The test signs with an
// HA1 derived from a *different* password the server doesn't know about,
// so check_digest_auth_digest() rejects with 401. This proves the server-
// side validation pathway is HA1-driven (it cannot have re-derived the
// wrong HA1 from cleartext, because it never received cleartext).
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

    std::string body1, headers1;
    long http_code1 = 0;  // NOLINT(runtime/int)
    CURL *curl1 = curl_easy_init();
    curl_easy_setopt(curl1, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl1, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl1, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl1, CURLOPT_WRITEDATA, &body1);
    curl_easy_setopt(curl1, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl1, CURLOPT_HEADERDATA, &headers1);
    curl_easy_setopt(curl1, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl1, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl1), 0);
    curl_easy_getinfo(curl1, CURLINFO_RESPONSE_CODE, &http_code1);
    LT_CHECK_EQ(http_code1, 401);
    curl_easy_cleanup(curl1);

    auto challenge = httpserver_test::extract_digest_challenge(headers1);
    LT_ASSERT_EQ(challenge.has_value(), true);

    // Derive HA1 from a password the server doesn't know about.
    unsigned char wrong_ha1[16];
    httpserver_test::detail::md5("myuser:examplerealm:totallywrong", wrong_ha1);

    std::string cnonce = httpserver_test::make_cnonce();
    std::string response = httpserver_test::compute_response_ha1(
        *challenge, httpserver_test::digest_hash::md5,
        "GET", "/base", wrong_ha1, 16, cnonce, "00000001");
    std::string auth_header = "Authorization: " +
        httpserver_test::build_authorization_header(
            *challenge, "myuser", "/base", cnonce, "00000001", response);

    std::string body2;
    long http_code2 = 0;  // NOLINT(runtime/int)
    CURL *curl2 = curl_easy_init();
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_header.c_str());
    curl_easy_setopt(curl2, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl2, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &body2);
    curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl2, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl2), 0);
    curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code2);
    LT_CHECK_EQ(http_code2, 401);
    LT_CHECK_EQ(body2, std::string("FAIL"));
    curl_easy_cleanup(curl2);
    curl_slist_free_all(hdrs);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_md5_wrong_pass)

// TASK-079: HA1-precomputed SHA-256 path. Same shape as the MD5 sibling
// (test signs with the configured 32-byte HA1 directly, never sends
// cleartext). Additionally asserts the algorithm token on the wire is
// "SHA-256" -- the canonical RFC 7616 §3.3 token, distinct from "MD5".
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

    std::string body1, headers1;
    long http_code1 = 0;  // NOLINT(runtime/int)
    CURL *curl1 = curl_easy_init();
    curl_easy_setopt(curl1, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl1, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl1, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl1, CURLOPT_WRITEDATA, &body1);
    curl_easy_setopt(curl1, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl1, CURLOPT_HEADERDATA, &headers1);
    curl_easy_setopt(curl1, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl1, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl1), 0);
    curl_easy_getinfo(curl1, CURLINFO_RESPONSE_CODE, &http_code1);
    LT_CHECK_EQ(http_code1, 401);
    curl_easy_cleanup(curl1);

    auto challenge = httpserver_test::extract_digest_challenge(headers1);
    LT_ASSERT_EQ(challenge.has_value(), true);
    LT_CHECK_EQ(challenge->algorithm, std::string("SHA-256"));

    std::string cnonce = httpserver_test::make_cnonce();
    std::string response = httpserver_test::compute_response_ha1(
        *challenge, httpserver_test::digest_hash::sha256,
        "GET", "/base", PRECOMPUTED_HA1_SHA256, 32, cnonce, "00000001");
    std::string auth_header = "Authorization: " +
        httpserver_test::build_authorization_header(
            *challenge, "myuser", "/base", cnonce, "00000001", response);

    std::string body2;
    long http_code2 = 0;  // NOLINT(runtime/int)
    CURL *curl2 = curl_easy_init();
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_header.c_str());
    curl_easy_setopt(curl2, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl2, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &body2);
    curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl2, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl2), 0);
    curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code2);
    LT_CHECK_EQ(http_code2, 200);
    LT_CHECK_EQ(body2, std::string("SUCCESS"));
    curl_easy_cleanup(curl2);
    curl_slist_free_all(hdrs);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_sha256)

// TASK-079: HA1-precomputed SHA-256 negative variant. Signs with a
// wrong-password-derived SHA-256 HA1; server rejects with 401.
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

    std::string body1, headers1;
    long http_code1 = 0;  // NOLINT(runtime/int)
    CURL *curl1 = curl_easy_init();
    curl_easy_setopt(curl1, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl1, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl1, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl1, CURLOPT_WRITEDATA, &body1);
    curl_easy_setopt(curl1, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl1, CURLOPT_HEADERDATA, &headers1);
    curl_easy_setopt(curl1, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl1, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl1), 0);
    curl_easy_getinfo(curl1, CURLINFO_RESPONSE_CODE, &http_code1);
    LT_CHECK_EQ(http_code1, 401);
    curl_easy_cleanup(curl1);

    auto challenge = httpserver_test::extract_digest_challenge(headers1);
    LT_ASSERT_EQ(challenge.has_value(), true);

    unsigned char wrong_ha1[32];
    httpserver_test::detail::sha256("myuser:examplerealm:totallywrong", wrong_ha1);

    std::string cnonce = httpserver_test::make_cnonce();
    std::string response = httpserver_test::compute_response_ha1(
        *challenge, httpserver_test::digest_hash::sha256,
        "GET", "/base", wrong_ha1, 32, cnonce, "00000001");
    std::string auth_header = "Authorization: " +
        httpserver_test::build_authorization_header(
            *challenge, "myuser", "/base", cnonce, "00000001", response);

    std::string body2;
    long http_code2 = 0;  // NOLINT(runtime/int)
    CURL *curl2 = curl_easy_init();
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_header.c_str());
    curl_easy_setopt(curl2, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl2, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &body2);
    curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl2, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl2), 0);
    curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code2);
    LT_CHECK_EQ(http_code2, 401);
    LT_CHECK_EQ(body2, std::string("FAIL"));
    curl_easy_cleanup(curl2);
    curl_slist_free_all(hdrs);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_with_ha1_sha256_wrong_pass)

// Resource that tests get_digested_user() caching.
// Covers http_request.cpp lines 293-295 (cache hit) and 300 (nullptr branch).
//
// TASK-079: migrated from the legacy `unauthorized("Digest", "testrealm",
// "FAIL")` static-string overload to the RFC 7616 `digest_challenge{...}`
// factory so the nonce/opaque handshake can complete. Once the client has
// authenticated, the resource validates via check_digest_auth() and then
// exercises both get_digested_user() entries (population + cache-hit).
class digest_user_cache_resource : public http_resource {
 public:
    http_response render_get(const http_request& req) {
        using httpserver::http::http_utils;
        using httpserver::digest_challenge;

        // First call: populates the cache from MHD (or returns empty if no
        // Authorization header has been sent yet).
        std::string user1 = std::string(req.get_digested_user());

        if (user1.empty()) {
            // No digest auth provided -- emit the initial RFC 7616 §3.3
            // challenge so a client can compute a response.
            return http_response::unauthorized(
                digest_challenge{.realm = "testrealm",
                                 .body  = "FAIL"});
        }

        // Validate the round-2 credentials. NOT_AUTHORIZED here means the
        // response token didn't verify against the configured password.
        auto result = req.check_digest_auth("testrealm", "testpass", 300,
                                            0, http_utils::digest_algorithm::MD5);
        if (result != http_utils::digest_auth_result::OK) {
            return http_response::unauthorized(
                digest_challenge{.realm = "testrealm",
                                 .body  = "FAIL"});
        }

        // Second call: must hit the cache (lines 293-295 in http_request.cpp).
        std::string user2 = std::string(req.get_digested_user());
        if (user1 != user2) {
            return http_response::string("CACHE_MISMATCH").with_status(500);
        }
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

// TASK-079: the resource emits a full RFC 7616 challenge, so the test can
// drive the handshake to completion. Round 2 carries a hand-built
// Authorization header; on the round-2 server-side validation pass,
// digest_user_cache_resource exercises BOTH legs of get_digested_user()
// caching (the populate + the cache-hit), returning "USER:testuser".
LT_BEGIN_AUTO_TEST(authentication_suite, digest_user_cache_with_auth)
    webserver ws{create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300)};

    auto resource = std::make_shared<digest_user_cache_resource>();
    ws.register_path("cache_test", resource);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);

    // Round 1 -- collect the challenge.
    std::string body1, headers1;
    long http_code1 = 0;  // NOLINT(runtime/int)
    CURL *curl1 = curl_easy_init();
    curl_easy_setopt(curl1, CURLOPT_URL, "localhost:" PORT_STRING "/cache_test");
    curl_easy_setopt(curl1, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl1, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl1, CURLOPT_WRITEDATA, &body1);
    curl_easy_setopt(curl1, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl1, CURLOPT_HEADERDATA, &headers1);
    curl_easy_setopt(curl1, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl1, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl1, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl1), 0);
    curl_easy_getinfo(curl1, CURLINFO_RESPONSE_CODE, &http_code1);
    LT_CHECK_EQ(http_code1, 401);
    curl_easy_cleanup(curl1);

    auto challenge = httpserver_test::extract_digest_challenge(headers1);
    LT_ASSERT_EQ(challenge.has_value(), true);

    // Round 2 -- sign with correct cleartext (testuser / testpass).
    std::string cnonce = httpserver_test::make_cnonce();
    std::string response = httpserver_test::compute_response_cleartext(
        *challenge, httpserver_test::digest_hash::md5,
        "GET", "/cache_test", "testuser", "testpass", cnonce, "00000001");
    std::string auth_header = "Authorization: " +
        httpserver_test::build_authorization_header(
            *challenge, "testuser", "/cache_test", cnonce, "00000001", response);

    std::string body2;
    long http_code2 = 0;  // NOLINT(runtime/int)
    CURL *curl2 = curl_easy_init();
    struct curl_slist *hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_header.c_str());
    curl_easy_setopt(curl2, CURLOPT_URL, "localhost:" PORT_STRING "/cache_test");
    curl_easy_setopt(curl2, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl2, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl2, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl2, CURLOPT_WRITEDATA, &body2);
    curl_easy_setopt(curl2, CURLOPT_TIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_CONNECTTIMEOUT, 150L);
    curl_easy_setopt(curl2, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl2, CURLOPT_NOSIGNAL, 1);
    LT_ASSERT_EQ(curl_easy_perform(curl2), 0);
    curl_easy_getinfo(curl2, CURLINFO_RESPONSE_CODE, &http_code2);
    LT_CHECK_EQ(http_code2, 200);
    LT_CHECK_EQ(body2, std::string("USER:testuser"));
    curl_easy_cleanup(curl2);
    curl_slist_free_all(hdrs);

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
