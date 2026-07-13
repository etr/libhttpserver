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

LT_BEGIN_SUITE(ip_access_control_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(ip_access_control_suite)

// The public IP access-control API is two symmetric list pairs:
//   deny_ip / remove_denied_ip   -> the deny list (exception under ACCEPT)
//   allow_ip / remove_allowed_ip -> the allow list (exception under REJECT;
//                                   also overrides a deny entry under ACCEPT)
// default_policy(ACCEPT|REJECT) selects which list is the exception list.

// Under the default ACCEPT policy: deny_ip refuses the peer, and
// remove_denied_ip re-admits it.
LT_BEGIN_AUTO_TEST(ip_access_control_suite, accept_default_deny_denies)
    webserver ws{create_webserver(0).default_policy(http_utils::ACCEPT)};
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string base_url = "localhost:" + std::to_string(port) + "/base";

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    {
    ws.deny_ip("127.0.0.1");

    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    {
    ws.remove_denied_ip("127.0.0.1");

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
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
LT_END_AUTO_TEST(accept_default_deny_denies)

// Under the default ACCEPT policy, an allow_ip entry overrides a matching
// deny_ip entry (allow wins) — the peer is admitted despite the deny.
LT_BEGIN_AUTO_TEST(ip_access_control_suite, accept_allow_overrides_deny)
    webserver ws{create_webserver(0).default_policy(http_utils::ACCEPT)};
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string base_url = "localhost:" + std::to_string(port) + "/base";

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    ws.deny_ip("127.0.0.1");
    ws.allow_ip("127.0.0.1");   // allow overrides deny

    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
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
LT_END_AUTO_TEST(accept_allow_overrides_deny)

// Under REJECT policy an IP that is neither allowed nor denied is refused
// (default-deny path).
LT_BEGIN_AUTO_TEST(ip_access_control_suite, reject_policy_neither_allowed_nor_denied)
    webserver ws{create_webserver(0).default_policy(http_utils::REJECT)};
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string base_url = "localhost:" + std::to_string(port) + "/base";

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    // IP is not in any list — REJECT policy should refuse.
    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(reject_policy_neither_allowed_nor_denied)

// Under REJECT policy, allow_ip admits the peer; remove_allowed_ip refuses
// it again. This is the allow-list mode that TASK-029 left unreachable.
LT_BEGIN_AUTO_TEST(ip_access_control_suite, reject_allow_admits_then_remove_refuses)
    webserver ws{create_webserver(0).default_policy(http_utils::REJECT)};
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string base_url = "localhost:" + std::to_string(port) + "/base";

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    // allow_ip permits the otherwise-rejected localhost peer.
    {
    ws.allow_ip("127.0.0.1");

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    // remove_allowed_ip drops it back to the default-deny path.
    {
    ws.remove_allowed_ip("127.0.0.1");

    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(reject_allow_admits_then_remove_refuses)

// deny_ip with wildcard then more specific IP.
// Drives the weight comparison branch in deny_ip.
LT_BEGIN_AUTO_TEST(ip_access_control_suite, deny_with_weight_comparison)
    webserver ws{create_webserver(0).default_policy(http_utils::ACCEPT)};
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string base_url = "localhost:" + std::to_string(port) + "/base";

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    // First deny with wildcard (lower weight).
    ws.deny_ip("127.0.0.*");

    // Then deny with more specific IP (higher weight).
    // This should hit the weight comparison branch.
    ws.deny_ip("127.0.0.1");

    // Request should still be denied.
    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(deny_with_weight_comparison)

// deny with specific IP first, then wildcard (lower weight replaces
// higher). Drives the t_ip.weight() < it->weight() erase-and-insert
// branch in deny_ip.
LT_BEGIN_AUTO_TEST(ip_access_control_suite, deny_specific_then_wildcard)
    webserver ws{create_webserver(0).default_policy(http_utils::ACCEPT)};
    ws.start(false);
    const uint16_t port = ws.get_bound_port();
    const std::string base_url = "localhost:" + std::to_string(port) + "/base";

    auto resource = std::make_shared<ok_resource>();
    ws.register_path("base", resource);

    curl_global_init(CURL_GLOBAL_ALL);

    // First deny specific IP (higher weight = 4).
    ws.deny_ip("127.0.0.1");

    // Then deny with wildcard (lower weight = 3).
    // This should trigger the erase-and-insert branch.
    ws.deny_ip("127.0.0.*");

    // Request should still be denied.
    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, base_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
LT_END_AUTO_TEST(deny_specific_then_wildcard)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
