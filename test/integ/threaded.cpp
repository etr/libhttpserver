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
#endif

#include <curl/curl.h>
#include <map>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using std::shared_ptr;

using httpserver::http_resource;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::string_response;
using httpserver::webserver;
using httpserver::create_webserver;

class ok_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return shared_ptr<string_response>(new string_response("OK", 200, "text/plain"));
     }
};

LT_BEGIN_SUITE(threaded_suite)

#ifndef _WINDOWS
    webserver* ws;
#endif

    void set_up() {
#ifndef _WINDOWS
        ws = new webserver(create_webserver(8080).start_method(httpserver::http::http_utils::INTERNAL_SELECT).max_threads(5));
        ws->start(false);
#endif
    }

    void tear_down() {
#ifndef _WINDOWS
        ws->stop();
        delete ws;
#endif
    }
LT_END_SUITE(threaded_suite)

LT_BEGIN_AUTO_TEST(threaded_suite, base)
#ifndef _WINDOWS
    ok_resource resource;
    ws->register_resource("base", &resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
#endif
LT_END_AUTO_TEST(base)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
