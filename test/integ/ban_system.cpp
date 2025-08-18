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

#if defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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
#if defined(__APPLE__)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 10);
    webserver ws = create_webserver(PORT).default_policy(http_utils::ACCEPT).bind_socket(fd);
#else
    webserver ws = create_webserver(PORT).default_policy(http_utils::ACCEPT);
#endif
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    {
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "127.0.0.1:" PORT_STRING "/base");
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
    curl_easy_setopt(curl, CURLOPT_URL, "127.0.0.1:" PORT_STRING "/base");
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
    curl_easy_setopt(curl, CURLOPT_URL, "127.0.0.1:" PORT_STRING "/base");
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
#if defined(__APPLE__)
    if (fd != -1) {
        close(fd);
    }
#endif
LT_END_AUTO_TEST(accept_default_ban_blocks)

LT_BEGIN_AUTO_TEST(ban_system_suite, reject_default_allow_passes)
#if defined(__APPLE__)
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 10);
    webserver ws = create_webserver(PORT).default_policy(http_utils::REJECT).bind_socket(fd);
#else
    webserver ws = create_webserver(PORT).default_policy(http_utils::REJECT);
#endif
    ws.start(false);

    ok_resource resource;
    LT_ASSERT_EQ(true, ws.register_resource("base", &resource));

    curl_global_init(CURL_GLOBAL_ALL);

    {
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "127.0.0.1:" PORT_STRING "/base");
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
    curl_easy_setopt(curl, CURLOPT_URL, "127.0.0.1:" PORT_STRING "/base");
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
    curl_easy_setopt(curl, CURLOPT_URL, "127.0.0.1:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_NEQ(res, 0);
    curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    ws.stop();
#if defined(__APPLE__)
    if (fd != -1) {
        close(fd);
    }
#endif
LT_END_AUTO_TEST(reject_default_allow_passes)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
