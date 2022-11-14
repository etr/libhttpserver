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

#include "./httpserver.hpp"
#include "./littletest.hpp"

#define MY_OPAQUE "11733b200778ce33060f31c9af70a870ba96ddd4"

using std::shared_ptr;
using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_response;
using httpserver::basic_auth_fail_response;
using httpserver::digest_auth_fail_response;
using httpserver::string_response;
using httpserver::http_resource;
using httpserver::http_request;

size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

class user_pass_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         if (req.get_user() != "myuser" || req.get_pass() != "mypass") {
             return shared_ptr<basic_auth_fail_response>(new basic_auth_fail_response("FAIL", "examplerealm"));
         }
         return shared_ptr<string_response>(new string_response(std::string(req.get_user()) + " " + std::string(req.get_pass()), 200, "text/plain"));
     }
};

class digest_resource : public http_resource {
 public:
     shared_ptr<http_response> render_GET(const http_request& req) {
         if (req.get_digested_user() == "") {
             return shared_ptr<digest_auth_fail_response>(new digest_auth_fail_response("FAIL", "examplerealm", MY_OPAQUE, true));
         } else {
             bool reload_nonce = false;
             if (!req.check_digest_auth("examplerealm", "mypass", 300, &reload_nonce)) {
                 return shared_ptr<digest_auth_fail_response>(new digest_auth_fail_response("FAIL", "examplerealm", MY_OPAQUE, reload_nonce));
             }
         }
         return shared_ptr<string_response>(new string_response("SUCCESS", 200, "text/plain"));
     }
};

LT_BEGIN_SUITE(authentication_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(authentication_suite)

LT_BEGIN_AUTO_TEST(authentication_suite, base_auth)
    webserver ws = create_webserver(8080);

    user_pass_resource user_pass;
    ws.register_resource("base", &user_pass);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_USERNAME, "myuser");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "mypass");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
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
    webserver ws = create_webserver(8080);

    user_pass_resource user_pass;
    ws.register_resource("base", &user_pass);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_USERNAME, "myuser");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "wrongpass");
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
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
#ifndef _WINDOWS

LT_BEGIN_AUTO_TEST(authentication_suite, digest_auth)
    webserver ws = create_webserver(8080)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300);

    digest_resource digest;
    ws.register_resource("base", &digest);
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
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
    webserver ws = create_webserver(8080)
        .digest_auth_random("myrandom")
        .nonce_nc_size(300);

    digest_resource digest;
    ws.register_resource("base", &digest);
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
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

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
