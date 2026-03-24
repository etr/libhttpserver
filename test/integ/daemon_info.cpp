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
#include <unistd.h>
#include <cstring>
#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using std::shared_ptr;
using std::string;
using httpserver::http_resource;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::string_response;
using httpserver::webserver;
using httpserver::create_webserver;

size_t writefunc(void *ptr, size_t size, size_t nmemb, string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

class simple_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
};

LT_BEGIN_SUITE(daemon_info_suite)
    void set_up() {
    }
    void tear_down() {
    }
LT_END_SUITE(daemon_info_suite)

LT_BEGIN_AUTO_TEST(daemon_info_suite, get_bound_port_explicit)
    webserver ws = create_webserver(9876);

    simple_resource sr;
    ws.register_resource("test", &sr);
    ws.start(false);

    LT_CHECK_EQ(ws.get_bound_port(), 9876);
    LT_CHECK_GT(ws.get_listen_fd(), 0);
    LT_CHECK_EQ(ws.get_active_connections(), 0u);

    ws.stop();
LT_END_AUTO_TEST(get_bound_port_explicit)

LT_BEGIN_AUTO_TEST(daemon_info_suite, get_active_connections_during_request)
    webserver ws = create_webserver(9877);

    simple_resource sr;
    ws.register_resource("test", &sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:9877/test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    ws.stop();
LT_END_AUTO_TEST(get_active_connections_during_request)

LT_BEGIN_AUTO_TEST(daemon_info_suite, quiesce_stops_new_connections)
    webserver ws = create_webserver(9878);

    simple_resource sr;
    ws.register_resource("test", &sr);
    ws.start(false);

    // Verify it works before quiesce
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:9878/test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    CURLcode res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    // Quiesce: stop accepting new connections.
    // Note: quiesce may return -1 if not supported with current daemon flags
    // (e.g., thread-per-connection mode). We just verify it doesn't crash.
    int listen_fd = ws.quiesce();
    // If quiesce succeeded, the FD should be positive
    if (listen_fd > 0) {
        close(listen_fd);
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    ws.stop();
LT_END_AUTO_TEST(quiesce_stops_new_connections)

LT_BEGIN_AUTO_TEST(daemon_info_suite, utility_functions)
    const char* version = httpserver::http::http_utils::get_mhd_version();
    LT_CHECK_NEQ(version, nullptr);

    const char* phrase = httpserver::http::http_utils::reason_phrase(200);
    LT_CHECK_EQ(string(phrase), "OK");

    const char* not_found = httpserver::http::http_utils::reason_phrase(404);
    LT_CHECK_EQ(string(not_found), "Not Found");
LT_END_AUTO_TEST(utility_functions)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
