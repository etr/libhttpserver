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
#include <pthread.h>
#include <unistd.h>

#include "./httpserver.hpp"
#include "./littletest.hpp"

using std::shared_ptr;

size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

class ok_resource : public httpserver::http_resource {
 public:
     shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         return shared_ptr<httpserver::string_response>(new httpserver::string_response("OK", 200, "text/plain"));
     }
};

shared_ptr<httpserver::http_response> not_found_custom(const httpserver::http_request&) {
    return shared_ptr<httpserver::string_response>(new httpserver::string_response("Not found custom", 404, "text/plain"));
}

shared_ptr<httpserver::http_response> not_allowed_custom(const httpserver::http_request&) {
    return shared_ptr<httpserver::string_response>(new httpserver::string_response("Not allowed custom", 405, "text/plain"));
}

LT_BEGIN_SUITE(ws_start_stop_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(ws_start_stop_suite)

#ifndef _WINDOWS

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, start_stop)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(8080);
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
    }

    {
    httpserver::webserver ws = httpserver::create_webserver(8080).start_method(httpserver::http::http_utils::INTERNAL_SELECT);
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
    }

    {
    httpserver::webserver ws = httpserver::create_webserver(8080).start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION);
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
    }
LT_END_AUTO_TEST(start_stop)

#if defined(IPV6_TESTS_ENABLED)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ipv6)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(8080).use_ipv6();
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
    }
LT_END_AUTO_TEST(ipv6)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, dual_stack)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(8080).use_dual_stack();
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
    }
LT_END_AUTO_TEST(dual_stack)

#endif

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, sweet_kill)
    httpserver::webserver ws = httpserver::create_webserver(8080);
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    {
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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    ws.sweet_kill();

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 7);
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(sweet_kill)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, disable_options)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .no_ssl()
        .no_ipv6()
        .no_debug()
        .no_pedantic()
        .no_basic_auth()
        .no_digest_auth()
        .no_deferred()
        .no_regex_checking()
        .no_ban_system()
        .no_post_process();
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(disable_options)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, enable_options)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .debug()
        .pedantic()
        .deferred()
        .regex_checking()
        .ban_system()
        .post_process();
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(enable_options)

#ifndef DARWIN
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, custom_socket)
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8181);
    bind(fd, (struct sockaddr*) &address, sizeof(address));
    listen(fd, 10000);

    httpserver::webserver ws = httpserver::create_webserver(-1).bind_socket(fd);  // whatever port here doesn't matter
    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8181/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(custom_socket)
#endif

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, single_resource)
    httpserver::webserver ws = httpserver::create_webserver(8080).single_resource();
    ok_resource ok;
    ws.register_resource("/", &ok, true);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/any/url/works");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(single_resource)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, single_resource_not_default_resource)
    httpserver::webserver ws = httpserver::create_webserver(8080).single_resource();
    ok_resource ok;
    LT_CHECK_THROW(ws.register_resource("/other", &ok, true));
    LT_CHECK_THROW(ws.register_resource("/", &ok, false));
    ws.start(false);

    ws.stop();
LT_END_AUTO_TEST(single_resource_not_default_resource)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, thread_per_connection_fails_with_max_threads)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION)
        .max_threads(5);
    LT_CHECK_THROW(ws.start(false));
    }
LT_END_AUTO_TEST(thread_per_connection_fails_with_max_threads)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, thread_per_connection_fails_with_max_threads_stack_size)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION)
        .max_thread_stack_size(4*1024*1024);
    LT_CHECK_THROW(ws.start(false));
    }
LT_END_AUTO_TEST(thread_per_connection_fails_with_max_threads_stack_size)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, tuning_options)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .max_connections(10)
        .max_threads(10)
        .memory_limit(10000)
        .per_IP_connection_limit(10)
        .max_thread_stack_size(4*1024*1024)
        .nonce_nc_size(10);

    ok_resource ok;
    ws.register_resource("base", &ok);
    LT_CHECK_NOTHROW(ws.start(false));

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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(tuning_options)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ssl_base)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .use_ssl()
        .https_mem_key("key.pem")
        .https_mem_cert("cert.pem");

    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    ws.stop();
LT_END_AUTO_TEST(ssl_base)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ssl_with_protocol_priorities)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .use_ssl()
        .https_mem_key("key.pem")
        .https_mem_cert("cert.pem")
        .https_priorities("NONE:+VERS-TLS1.0:+AES-128-CBC:+SHA1:+RSA:+COMP-NULL");

    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(ssl_with_protocol_priorities)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ssl_with_trust)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .use_ssl()
        .https_mem_key("key.pem")
        .https_mem_cert("cert.pem")
        .https_mem_trust("test_root_ca.pem");

    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(ssl_with_trust)

void* start_ws_blocking(void* par) {
    httpserver::webserver* ws = (httpserver::webserver*) par;
    ok_resource ok;
    ws->register_resource("base", &ok);
    ws->start(true);

    return nullptr;
}

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, blocking_server)
    httpserver::webserver ws = httpserver::create_webserver(8080);

    pthread_t tid;
    pthread_create(&tid, nullptr, start_ws_blocking, reinterpret_cast<void*>(&ws));

    sleep(1);

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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();

    char* b;
    pthread_join(tid, reinterpret_cast<void**>(&b));
    free(b);
LT_END_AUTO_TEST(blocking_server)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, custom_error_resources)
    httpserver::webserver ws = httpserver::create_webserver(8080)
        .not_found_resource(not_found_custom)
        .method_not_allowed_resource(not_allowed_custom);

    ok_resource ok;
    ws.register_resource("base", &ok);
    ws.start(false);

    {
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
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    }

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/not_registered");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Not found custom");

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 404);

    curl_easy_cleanup(curl);
    }

    {
    ok.set_allowing("PUT", false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "Not allowed custom");

    int64_t http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_ASSERT_EQ(http_code, 405);

    curl_easy_cleanup(curl);
    }

    ws.stop();
LT_END_AUTO_TEST(custom_error_resources)

#endif

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
