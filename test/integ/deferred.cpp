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
#include <signal.h>
#include <unistd.h>

#include <cstring>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using std::shared_ptr;
using std::string;

using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_response;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::deferred_response;

size_t writefunc(void *ptr, size_t size, size_t nmemb, string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

static int counter = 0;

struct test_data {
    int value;
};

ssize_t test_callback(shared_ptr<void> closure_data, char* buf, size_t max) {
    std::ignore = closure_data;

    if (counter == 2) {
        return -1;
    } else {
        memset(buf, 0, max);
        snprintf(buf, max, "%s", "test");
        counter++;
        return string(buf).size();
    }
}

ssize_t test_callback_with_data(shared_ptr<test_data> closure_data, char* buf, size_t max) {
    if (counter == 2) {
        return -1;
    } else {
        memset(buf, 0, max);
        snprintf(buf, max, "%s%s", "test", std::to_string(closure_data->value).c_str());

        closure_data->value = 84;

        counter++;
        return std::string(buf).size();
    }
}

class deferred_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<deferred_response<void>>(new deferred_response<void>(test_callback, nullptr, "cycle callback response"));
     }
};

class deferred_resource_with_data : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         shared_ptr<test_data> internal_info(new test_data);
         internal_info->value = 42;
         return shared_ptr<deferred_response<test_data>>(new deferred_response<test_data>(test_callback_with_data, internal_info, "cycle callback response"));
     }
};

LT_BEGIN_SUITE(deferred_suite)
    webserver* ws;

    void set_up() {
        ws = new webserver(create_webserver(8080));
        ws->start(false);
    }

    void tear_down() {
        counter = 0;

        ws->stop();
        delete ws;
    }
LT_END_SUITE(deferred_suite)

LT_BEGIN_AUTO_TEST(deferred_suite, deferred_response_suite)
    deferred_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "testtest");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(deferred_response_suite)

LT_BEGIN_AUTO_TEST(deferred_suite, deferred_response_with_data)
    deferred_resource_with_data resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);

    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "test42test84");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(deferred_response_with_data)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
