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
#include "./littletest.hpp"

using std::shared_ptr;

using httpserver::http_resource;
using httpserver::http_response;
using httpserver::string_response;
using httpserver::http_request;
using httpserver::http_resource;
using httpserver::webserver;
using httpserver::create_webserver;

#ifdef HTTPSERVER_PORT
#define PORT HTTPSERVER_PORT
#else
#define PORT 8080
#endif  // PORT

#define STR2(p) #p
#define STR(p) STR2(p)
#define PORT_STRING STR(PORT)

class ok_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<string_response>("OK", 200, "text/plain");
     }
};

LT_BEGIN_SUITE(threaded_suite)
    std::unique_ptr<webserver> ws;

    void set_up() {
        ws = std::make_unique<webserver>(create_webserver(PORT));
        ws->start(false);
    }

    void tear_down() {
        ws->stop();
    }
LT_END_SUITE(threaded_suite)

LT_BEGIN_AUTO_TEST(threaded_suite, base)
    ok_resource resource;
    LT_ASSERT_EQ(true, ws->register_resource("base", &resource));
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "127.0.0.1:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(base)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
