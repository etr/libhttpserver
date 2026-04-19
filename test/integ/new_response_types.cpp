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
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using std::shared_ptr;
using std::string;
using std::vector;
using httpserver::http_resource;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::empty_response;
using httpserver::pipe_response;
using httpserver::iovec_response;
using httpserver::webserver;
using httpserver::create_webserver;

#ifdef HTTPSERVER_PORT
#define PORT HTTPSERVER_PORT
#else
#define PORT 8080
#endif

#define STR2(p) #p
#define STR(p) STR2(p)
#define PORT_STRING STR(PORT)

size_t writefunc(void *ptr, size_t size, size_t nmemb, string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

class empty_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         return std::make_shared<empty_response>(204);
     }
};

class pipe_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         int pipefd[2];
#if defined(_WIN32) && !defined(__CYGWIN__)
         if (_pipe(pipefd, 4096, _O_BINARY) != 0) {
#else
         if (pipe(pipefd) != 0) {
#endif
             return std::make_shared<empty_response>(500);
         }
         const char* msg = "hello from pipe";
         write(pipefd[1], msg, strlen(msg));
         close(pipefd[1]);
         return std::make_shared<pipe_response>(pipefd[0], 200);
     }
};  // NOLINT(readability/braces)

class iovec_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request&) {
         vector<string> parts = {"Hello", " ", "World"};
         return std::make_shared<iovec_response>(parts, 200, "text/plain");
     }
};

static webserver* ws_ptr = nullptr;
static empty_resource er;
static pipe_resource pr;
static iovec_resource ir;

LT_BEGIN_SUITE(response_types_suite)
    void set_up() {
        ws_ptr = new webserver(create_webserver(PORT));
        ws_ptr->register_resource("empty", &er);
        ws_ptr->register_resource("pipe", &pr);
        ws_ptr->register_resource("iovec", &ir);
        ws_ptr->start(false);
    }
    void tear_down() {
        ws_ptr->stop();
        delete ws_ptr;
        ws_ptr = nullptr;
    }
LT_END_SUITE(response_types_suite)

LT_BEGIN_AUTO_TEST(response_types_suite, empty_response_test)
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/empty");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 204);
    LT_CHECK_EQ(s, "");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
LT_END_AUTO_TEST(empty_response_test)

LT_BEGIN_AUTO_TEST(response_types_suite, pipe_response_test)
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/pipe");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "hello from pipe");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
LT_END_AUTO_TEST(pipe_response_test)

LT_BEGIN_AUTO_TEST(response_types_suite, iovec_response_test)
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/iovec");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "Hello World");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
LT_END_AUTO_TEST(iovec_response_test)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
