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

#include <curl/curl.h>
#include <map>
#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "httpserver/http_utils.hpp"
#include "./littletest.hpp"

using std::shared_ptr;

using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_resource;
using httpserver::http_response;
using httpserver::string_response;
using httpserver::http_request;
using httpserver::http::http_utils;

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

class ok_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
};

LT_BEGIN_SUITE(ban_system_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(ban_system_suite)

LT_BEGIN_AUTO_TEST(ban_system_suite, accept_default_ban_blocks)
    webserver ws = create_webserver(PORT).default_policy(http_utils::ACCEPT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    {
    ws.ban_ip("127.0.0.1");

    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    ws.unban_ip("127.0.0.1");

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(accept_default_ban_blocks)

LT_BEGIN_AUTO_TEST(ban_system_suite, reject_default_allow_passes)
    webserver ws = create_webserver(PORT).default_policy(http_utils::REJECT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    ws.allow_ip("127.0.0.1");

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    {
    ws.disallow_ip("127.0.0.1");

    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(reject_default_allow_passes)

// Test ACCEPT policy with IP on allow list - allow overrides ban
// In ACCEPT mode: condition is (is_banned && !is_allowed)
// If IP is on allow list, !is_allowed is false, so connection is always allowed
LT_BEGIN_AUTO_TEST(ban_system_suite, accept_policy_allow_overrides_ban)
    webserver ws = create_webserver(PORT).default_policy(http_utils::ACCEPT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    // Add IP to allow list
    ws.allow_ip("127.0.0.1");

    // Request should work (ACCEPT policy + on allow list)
    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    // Ban the IP - but in ACCEPT mode, allow list overrides ban
    ws.ban_ip("127.0.0.1");

    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);  // Still allowed - allow list overrides ban in ACCEPT mode
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    // Remove from allow list - now ban should take effect
    ws.disallow_ip("127.0.0.1");

    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);  // Now blocked - ban takes effect without allow list
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(accept_policy_allow_overrides_ban)

// Test REJECT policy with IP that is allowed but then banned
// Tests: (!is_allowed || is_banned) - banned overrides allowed
LT_BEGIN_AUTO_TEST(ban_system_suite, reject_policy_allowed_then_banned)
    webserver ws = create_webserver(PORT).default_policy(http_utils::REJECT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    // First, IP is not allowed - should be blocked
    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    // Allow the IP - should work
    ws.allow_ip("127.0.0.1");

    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    // Now ban the IP - ban should override allow
    ws.ban_ip("127.0.0.1");

    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);  // Should be blocked
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(reject_policy_allowed_then_banned)

// Test REJECT policy with IP that is neither allowed nor banned
// Tests default REJECT behavior
LT_BEGIN_AUTO_TEST(ban_system_suite, reject_policy_neither_allowed_nor_banned)
    webserver ws = create_webserver(PORT).default_policy(http_utils::REJECT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    // IP is not in any list - REJECT policy should block
    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(reject_policy_neither_allowed_nor_banned)

// Test ban/allow with wildcard then more specific IP
// This tests the weight comparison branch in ban_ip/allow_ip
LT_BEGIN_AUTO_TEST(ban_system_suite, ban_with_weight_comparison)
    webserver ws = create_webserver(PORT).default_policy(http_utils::ACCEPT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    // First ban with wildcard (lower weight)
    ws.ban_ip("127.0.0.*");

    // Then ban with more specific IP (higher weight)
    // This should hit the weight comparison branch
    ws.ban_ip("127.0.0.1");

    // Request should still be blocked
    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(ban_with_weight_comparison)

// Test allow with wildcard then more specific IP
LT_BEGIN_AUTO_TEST(ban_system_suite, allow_with_weight_comparison)
    webserver ws = create_webserver(PORT).default_policy(http_utils::REJECT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    // First allow with wildcard (lower weight)
    ws.allow_ip("127.0.0.*");

    // Then allow with more specific IP (higher weight)
    // This should hit the weight comparison branch
    ws.allow_ip("127.0.0.1");

    // Request should be allowed
    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(allow_with_weight_comparison)

// Test ban with specific IP first, then wildcard (lower weight replaces higher)
// This tests the t_ip.weight() < (*it).weight() branch
LT_BEGIN_AUTO_TEST(ban_system_suite, ban_specific_then_wildcard)
    webserver ws = create_webserver(PORT).default_policy(http_utils::ACCEPT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    // First ban specific IP (higher weight = 4)
    ws.ban_ip("127.0.0.1");

    // Then ban with wildcard (lower weight = 3)
    // This should trigger the erase-and-insert branch
    ws.ban_ip("127.0.0.*");

    // Request should still be blocked
    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(ban_specific_then_wildcard)

// Test allow with specific IP first, then wildcard (lower weight replaces higher)
LT_BEGIN_AUTO_TEST(ban_system_suite, allow_specific_then_wildcard)
    webserver ws = create_webserver(PORT).default_policy(http_utils::REJECT);
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    // First allow specific IP (higher weight = 4)
    ws.allow_ip("127.0.0.1");

    // Then allow with wildcard (lower weight = 3)
    // This should trigger the erase-and-insert branch
    ws.allow_ip("127.0.0.*");

    // Request should be allowed
    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(allow_specific_then_wildcard)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
