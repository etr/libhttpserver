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
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"

#define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

using std::shared_ptr;
using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_response;
using httpserver::basic_auth_fail_response;
#ifdef HAVE_DAUTH
using httpserver::digest_auth_fail_response;
#endif  // HAVE_DAUTH
using httpserver::string_response;
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

class user_pass_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         if (req.get_user() != "myuser" || req.get_pass() != "mypass") {
             return std::make_shared<basic_auth_fail_response>("FAIL", "examplerealm");
         }
         return std::make_shared<string_response>(std::string(req.get_user()) + " " + std::string(req.get_pass()), 200, "text/plain");
     }
};

#ifdef HAVE_DAUTH
class digest_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         if (req.get_digested_user() == "") {
             return std::make_shared<digest_auth_fail_response>("FAIL", "examplerealm", MY_OPAQUE, true);
         } else {
             bool reload_nonce = false;
             if (!req.check_digest_auth("examplerealm", "mypass", 300, &reload_nonce)) {
                 return std::make_shared<digest_auth_fail_response>("FAIL", "examplerealm", MY_OPAQUE, reload_nonce);
             }
         }
         return std::make_shared<string_response>("SUCCESS", 200, "text/plain");
     }
};
#endif  // HAVE_DAUTH

LT_BEGIN_SUITE(authentication_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(authentication_suite)

LT_BEGIN_AUTO_TEST(authentication_suite, base_auth)
    webserver ws = create_webserver(PORT);

    user_pass_resource user_pass;
    LT_ASSERT_EQ(true, ws.register_resource("base", &user_pass));
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
    webserver ws = create_webserver(PORT);

    user_pass_resource user_pass;
    LT_ASSERT_EQ(true, ws.register_resource("base", &user_pass));
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

//  do not run the digest auth tests on windows as curl
//  appears to have problems with it.
//  Will fix this separately
//  Also skip if libmicrohttpd was built without digest auth support
#if !defined(_WINDOWS) && defined(HAVE_DAUTH)

LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth)
    webserver ws = create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300);

    digest_resource digest;
    LT_ASSERT_EQ(true, ws.register_resource("base", &digest));
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
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
    LT_CHECK_EQ(s, "SUCCESS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth)

LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth_wrong_pass)
    webserver ws = create_webserver(PORT)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300);

    digest_resource digest;
    LT_ASSERT_EQ(true, ws.register_resource("base", &digest));
    ws.start(false);

#if defined(_WINDOWS)
    curl_global_init(CURL_GLOBAL_WIN32);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
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
    LT_CHECK_EQ(s, "FAIL");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_wrong_pass)

#endif

// Simple resource for centralized auth tests
class simple_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("SUCCESS", 200, "text/plain");
     }
};

// Centralized authentication handler
std::shared_ptr<http_response> centralized_auth_handler(const http_request& req) {
    if (req.get_user() != "admin" || req.get_pass() != "secret") {
        return std::make_shared<basic_auth_fail_response>("Unauthorized", "testrealm");
    }
    return nullptr;  // Allow request
}

LT_BEGIN_AUTO_TEST(authentication_suite, centralized_auth_fail)
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler);

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("protected", &sr));
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
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler);

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("protected", &sr));
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
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/health", "/public/*"});

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("health", &sr));
    LT_ASSERT_EQ(true, ws.register_resource("public/info", &sr));
    LT_ASSERT_EQ(true, ws.register_resource("protected", &sr));
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
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/public/*"});

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("publicinfo", &sr));
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
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/api/v1/public/*"});

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("api/v1/public/users/list", &sr));
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
     shared_ptr<http_response> render_POST(const http_request&) {
         return std::make_shared<string_response>("POST_SUCCESS", 200, "text/plain");
     }
};

LT_BEGIN_AUTO_TEST(authentication_suite, centralized_auth_post_method)
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler);

    post_resource pr;
    LT_ASSERT_EQ(true, ws.register_resource("data", &pr));
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
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler);

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("protected", &sr));
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
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler);

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("exists", &sr));
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
    webserver ws = create_webserver(PORT);  // No auth_handler

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("open", &sr));
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
    webserver ws = create_webserver(PORT)
        .auth_handler(centralized_auth_handler)
        .auth_skip_paths({"/health", "/metrics", "/status", "/public/*"});

    simple_resource sr;
    LT_ASSERT_EQ(true, ws.register_resource("health", &sr));
    LT_ASSERT_EQ(true, ws.register_resource("metrics", &sr));
    LT_ASSERT_EQ(true, ws.register_resource("status", &sr));
    LT_ASSERT_EQ(true, ws.register_resource("protected", &sr));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl;
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    std::string s;

    // All skip paths should work without auth
    const char* skip_urls[] = {"/health", "/metrics", "/status"};
    for (const char* url : skip_urls) {
        curl = curl_easy_init();
        s = "";
        http_code = 0;
        std::string full_url = std::string("localhost:" PORT_STRING) + url;
        curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(http_code, 200);
        curl_easy_cleanup(curl);
    }

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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
