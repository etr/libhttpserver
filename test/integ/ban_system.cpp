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
#include "./test_utils.hpp"

using std::shared_ptr;

using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_resource;
using httpserver::http_response;
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
     http_response render_get(const http_request&) {
         return http_response::string("OK");
     }
};

LT_BEGIN_SUITE(ban_system_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(ban_system_suite)

// TASK-029: The public IP-control API is the pair block_ip / unblock_ip.
// The historical allow_ip / disallow_ip pair was removed from the public
// surface (see PRD-NAM-REQ-005 and OQ-004). The internal allow-list code
// path inside policy_callback remains intact so default_policy(REJECT)
// semantics keep working at the daemon level, but it is no longer
// reachable through the public API in v2.0 — five previous tests that
// drove that path through the public API were deleted along with the
// allow_ip / disallow_ip symbols.

LT_BEGIN_AUTO_TEST(ban_system_suite, accept_default_block_blocks)
    webserver ws{create_webserver(PORT).default_policy(http_utils::ACCEPT)};
    ws.start(false);

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

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
    ws.block_ip("127.0.0.1");

    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    ws.unblock_ip("127.0.0.1");

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
LT_END_AUTO_TEST(accept_default_block_blocks)

// Test REJECT policy with IP that is neither allowed nor blocked.
// Tests default REJECT behavior — drives policy_callback's default path
// without touching the (now-private) allow list.
LT_BEGIN_AUTO_TEST(ban_system_suite, reject_policy_neither_allowed_nor_blocked)
    webserver ws{create_webserver(PORT).default_policy(http_utils::REJECT)};
    ws.start(false);

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    // IP is not in any list — REJECT policy should block.
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
LT_END_AUTO_TEST(reject_policy_neither_allowed_nor_blocked)

// Test block_ip with wildcard then more specific IP.
// Drives the weight comparison branch in block_ip.
LT_BEGIN_AUTO_TEST(ban_system_suite, block_with_weight_comparison)
    webserver ws{create_webserver(PORT).default_policy(http_utils::ACCEPT)};
    ws.start(false);

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    // First block with wildcard (lower weight).
    ws.block_ip("127.0.0.*");

    // Then block with more specific IP (higher weight).
    // This should hit the weight comparison branch.
    ws.block_ip("127.0.0.1");

    // Request should still be blocked.
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
LT_END_AUTO_TEST(block_with_weight_comparison)

// Test block with specific IP first, then wildcard (lower weight replaces
// higher). Drives the t_ip.weight() < (*it).weight() erase-and-insert
// branch in block_ip.
LT_BEGIN_AUTO_TEST(ban_system_suite, block_specific_then_wildcard)
    webserver ws{create_webserver(PORT).default_policy(http_utils::ACCEPT)};
    ws.start(false);

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    // First block specific IP (higher weight = 4).
    ws.block_ip("127.0.0.1");

    // Then block with wildcard (lower weight = 3).
    // This should trigger the erase-and-insert branch.
    ws.block_ip("127.0.0.*");

    // Request should still be blocked.
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
LT_END_AUTO_TEST(block_specific_then_wildcard)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
