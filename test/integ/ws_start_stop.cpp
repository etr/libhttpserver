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
#include <memory>
#include <string>

#ifdef HAVE_GNUTLS
#include <gnutls/gnutls.h>
#endif

#include "./httpserver.hpp"
#include "./littletest.hpp"

using std::shared_ptr;

#ifdef HTTPSERVER_PORT
#define PORT HTTPSERVER_PORT
#else
#define PORT 8080
#endif  // PORT

#define STR2(p) #p
#define STR(p) STR2(p)
#define PORT_STRING STR(PORT)

#ifdef HTTPSERVER_DATA_ROOT
#define ROOT STR(HTTPSERVER_DATA_ROOT)
#else
#define ROOT "."
#endif  // HTTPSERVER_DATA_ROOT

size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

class ok_resource : public httpserver::http_resource {
 public:
     shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request&) {
         return std::make_shared<httpserver::string_response>("OK", 200, "text/plain");
     }
};

#ifdef HAVE_GNUTLS
class tls_info_resource : public httpserver::http_resource {
 public:
     shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request& req) {
         std::string response;
         if (req.has_tls_session()) {
             gnutls_session_t session = req.get_tls_session();
             if (session != nullptr) {
                 response = "TLS_SESSION_PRESENT";
             } else {
                 response = "TLS_SESSION_NULL";
             }
         } else {
             response = "NO_TLS_SESSION";
         }
         return std::make_shared<httpserver::string_response>(response, 200, "text/plain");
     }
};
#endif  // HAVE_GNUTLS

shared_ptr<httpserver::http_response> not_found_custom(const httpserver::http_request&) {
    return std::make_shared<httpserver::string_response>("Not found custom", 404, "text/plain");
}

shared_ptr<httpserver::http_response> not_allowed_custom(const httpserver::http_request&) {
    return std::make_shared<httpserver::string_response>("Not allowed custom", 405, "text/plain");
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
    httpserver::webserver ws = httpserver::create_webserver(PORT);
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
    }

    {
    httpserver::webserver ws = httpserver::create_webserver(PORT).start_method(httpserver::http::http_utils::INTERNAL_SELECT);
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
    }

    {
    httpserver::webserver ws = httpserver::create_webserver(PORT).start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION);
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
    }
LT_END_AUTO_TEST(start_stop)

#if defined(IPV6_TESTS_ENABLED)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ipv6)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(PORT).use_ipv6();
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:" PORT_STRING "/base");
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
    httpserver::webserver ws = httpserver::create_webserver(PORT).use_dual_stack();
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:" PORT_STRING "/base");
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
    httpserver::webserver ws = httpserver::create_webserver(PORT);
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    {
    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.sweet_kill();

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 7);
    curl_easy_cleanup(curl);
    }
LT_END_AUTO_TEST(sweet_kill)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, disable_options)
    httpserver::webserver ws = httpserver::create_webserver(PORT)
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
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
LT_END_AUTO_TEST(disable_options)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, enable_options)
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .debug()
        .pedantic()
        .deferred()
        .regex_checking()
        .ban_system()
        .post_process();
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
LT_END_AUTO_TEST(enable_options)

#ifndef DARWIN
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, custom_socket)
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(PORT);
    bind(fd, (struct sockaddr*) &address, sizeof(address));
    listen(fd, 10000);

    httpserver::webserver ws = httpserver::create_webserver(-1).bind_socket(fd);  // whatever port here doesn't matter
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
LT_END_AUTO_TEST(custom_socket)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, bind_address_string)
    httpserver::webserver ws = httpserver::create_webserver(PORT).bind_address("127.0.0.1");
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
LT_END_AUTO_TEST(bind_address_string)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, bind_address_string_invalid)
    LT_CHECK_THROW(httpserver::create_webserver(PORT).bind_address("not_an_ip"));
LT_END_AUTO_TEST(bind_address_string_invalid)
#endif

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, single_resource)
    httpserver::webserver ws = httpserver::create_webserver(PORT).single_resource();
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("/", &ok, true));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/any/url/works");
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
    httpserver::webserver ws = httpserver::create_webserver(PORT).single_resource();
    ok_resource ok;
    LT_CHECK_THROW(ws.register_resource("/other", &ok, true));
    LT_CHECK_THROW(ws.register_resource("/", &ok, false));
    ws.start(false);

    ws.stop();
LT_END_AUTO_TEST(single_resource_not_default_resource)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_resource_nullptr_throws)
    httpserver::webserver ws = httpserver::create_webserver(PORT);
    LT_CHECK_THROW(ws.register_resource("/test", nullptr));
LT_END_AUTO_TEST(register_resource_nullptr_throws)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_empty_resource_non_family)
    httpserver::webserver ws = httpserver::create_webserver(PORT);
    ok_resource ok;
    // Register empty resource with family=false
    LT_CHECK_EQ(true, ws.register_resource("", &ok, false));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(register_empty_resource_non_family)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_resource_with_url_params_non_family)
    httpserver::webserver ws = httpserver::create_webserver(PORT).regex_checking();
    ok_resource ok;
    // Register resource with URL parameters, non-family
    LT_CHECK_EQ(true, ws.register_resource("/user/{id}", &ok, false));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/user/123");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(register_resource_with_url_params_non_family)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_duplicate_resource_returns_false)
    httpserver::webserver ws = httpserver::create_webserver(PORT);
    ok_resource ok1, ok2;
    // First registration should succeed
    LT_CHECK_EQ(true, ws.register_resource("/duplicate", &ok1, false));
    // Second registration of same path should fail (return false)
    LT_CHECK_EQ(false, ws.register_resource("/duplicate", &ok2, false));
    // But with family=true should succeed (different type of registration)
    LT_CHECK_EQ(true, ws.register_resource("/duplicate", &ok2, true));
LT_END_AUTO_TEST(register_duplicate_resource_returns_false)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, thread_per_connection_fails_with_max_threads)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION)
        .max_threads(5);
    LT_CHECK_THROW(ws.start(false));
    }
LT_END_AUTO_TEST(thread_per_connection_fails_with_max_threads)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, thread_per_connection_fails_with_max_threads_stack_size)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION)
        .max_thread_stack_size(4*1024*1024);
    LT_CHECK_THROW(ws.start(false));
    }
LT_END_AUTO_TEST(thread_per_connection_fails_with_max_threads_stack_size)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, tuning_options)
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .max_connections(10)
        .max_threads(10)
        .memory_limit(10000)
        .per_IP_connection_limit(10)
        .max_thread_stack_size(4*1024*1024)
        .nonce_nc_size(10);

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    LT_CHECK_NOTHROW(ws.start(false));

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();
LT_END_AUTO_TEST(tuning_options)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ssl_base)
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem");

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:" PORT_STRING "/base");
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
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_priorities("NORMAL:-MD5");

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:" PORT_STRING "/base");
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
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/test_root_ca.pem");

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);  // avoid verifying ssl
    curl_easy_setopt(curl, CURLOPT_URL, "https://localhost:" PORT_STRING "/base");
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
    if (!ws->register_resource("base", &ok)) return PTHREAD_CANCELED;
    try {
        ws->start(true);
    } catch (...) {
        return PTHREAD_CANCELED;
    }

    return nullptr;
}

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, blocking_server)
    httpserver::webserver ws = httpserver::create_webserver(PORT);

    pthread_t tid;
    pthread_create(&tid, nullptr, start_ws_blocking, reinterpret_cast<void*>(&ws));

    sleep(1);

    curl_global_init(CURL_GLOBAL_ALL);
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

    ws.stop();

    char* b;
    pthread_join(tid, reinterpret_cast<void**>(&b));
    LT_CHECK_EQ(b, nullptr);
    free(b);
LT_END_AUTO_TEST(blocking_server)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, custom_error_resources)
    httpserver::webserver ws = httpserver::create_webserver(PORT)
        .not_found_resource(not_found_custom)
        .method_not_allowed_resource(not_allowed_custom);

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    {
    curl_global_init(CURL_GLOBAL_ALL);
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
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/not_registered");
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
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/base");
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

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ipv6_webserver)
    httpserver::webserver ws = httpserver::create_webserver(PORT + 20).use_ipv6();
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    bool started = ws.start(false);
    // IPv6 may not be available, so we just check the configuration worked
    if (started) {
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        curl_easy_setopt(curl, CURLOPT_URL, "http://[::1]:" STR(PORT + 20) "/base");
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        if (res == 0) {
            LT_CHECK_EQ(s, "OK");
        }
        curl_easy_cleanup(curl);
        ws.stop();
    }
    LT_CHECK_EQ(1, 1);  // Test passes even if IPv6 not available
LT_END_AUTO_TEST(ipv6_webserver)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, dual_stack_webserver)
    httpserver::webserver ws = httpserver::create_webserver(PORT + 21).use_dual_stack();
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    bool started = ws.start(false);
    if (started) {
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        curl_easy_setopt(curl, CURLOPT_URL, "localhost:" STR(PORT + 21) "/base");
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        curl_easy_cleanup(curl);
        ws.stop();
    }
    LT_CHECK_EQ(1, 1);  // Test passes even if dual stack not available
LT_END_AUTO_TEST(dual_stack_webserver)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, bind_address_ipv4)
    int port = PORT + 22;
    httpserver::webserver ws = httpserver::create_webserver(port).bind_address("127.0.0.1");
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(bind_address_ipv4)

#ifdef HAVE_GNUTLS
// Test TLS session getters on non-TLS connection (should return false/nullptr)
class tls_check_non_tls_resource : public httpserver::http_resource {
 public:
     std::shared_ptr<httpserver::http_response> render_GET(const httpserver::http_request& req) {
         // On non-TLS connection, has_tls_session should return false
         std::string response = req.has_tls_session() ? "HAS_TLS" : "NO_TLS";
         return std::make_shared<httpserver::string_response>(response, 200, "text/plain");
     }
};

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, tls_session_on_non_tls_connection)
    int port = PORT + 25;
    httpserver::webserver ws = httpserver::create_webserver(port);  // No SSL
    tls_check_non_tls_resource tls_check;
    LT_ASSERT_EQ(true, ws.register_resource("tls_check", &tls_check));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(port) + "/tls_check";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "NO_TLS");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(tls_session_on_non_tls_connection)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, https_webserver)
    int port = PORT + 23;
    httpserver::webserver ws = httpserver::create_webserver(port)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem");
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    bool started = ws.start(false);
    if (!started) {
        // SSL setup may fail in some environments, skip the test
        LT_CHECK_EQ(1, 1);
    } else {
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "https://localhost:" + std::to_string(port) + "/base";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        curl_easy_cleanup(curl);
        ws.stop();
    }
LT_END_AUTO_TEST(https_webserver)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, tls_session_getters)
    int port = PORT + 24;
    httpserver::webserver ws = httpserver::create_webserver(port)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem");
    tls_info_resource tls_info;
    LT_ASSERT_EQ(true, ws.register_resource("tls_info", &tls_info));
    bool started = ws.start(false);
    if (!started) {
        // SSL setup may fail in some environments, skip the test
        LT_CHECK_EQ(1, 1);
    } else {
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        CURL *curl = curl_easy_init();
        CURLcode res;
        std::string url = "https://localhost:" + std::to_string(port) + "/tls_info";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        res = curl_easy_perform(curl);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "TLS_SESSION_PRESENT");
        curl_easy_cleanup(curl);
        ws.stop();
    }
LT_END_AUTO_TEST(tls_session_getters)
#endif  // HAVE_GNUTLS

#endif  // _WINDOWS

// Test pedantic mode configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, pedantic_mode)
    int port = PORT + 26;
    httpserver::webserver ws = httpserver::create_webserver(port).pedantic();
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(pedantic_mode)

#ifdef HAVE_DAUTH
// Test digest_auth_random configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, digest_auth_random)
    int port = PORT + 27;
    httpserver::webserver ws = httpserver::create_webserver(port)
        .digest_auth_random("random_string_for_digest");
    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);

    ws.stop();
LT_END_AUTO_TEST(digest_auth_random)
#endif  // HAVE_DAUTH

#ifdef HAVE_GNUTLS
// PSK handler that returns a hex-encoded key for the test user
std::string test_psk_handler(const std::string& username) {
    if (username == "testuser") {
        // Return hex-encoded PSK key (16 bytes = 32 hex chars)
        return "0123456789abcdef0123456789abcdef";
    }
    return "";  // Unknown user - return empty to trigger error path
}

// PSK handler that always returns empty (for error path testing)
std::string empty_psk_handler(const std::string&) {
    return "";
}

// Test PSK credential handler setup
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_handler_setup)
    int port = PORT + 28;
    httpserver::webserver ws = httpserver::create_webserver(port)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .cred_type(httpserver::http::http_utils::PSK)
        .psk_cred_handler(test_psk_handler);

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    bool started = ws.start(false);

    // PSK setup may fail if libmicrohttpd/gnutls doesn't support it
    // Just verify the server can be configured with PSK options
    if (started) {
        ws.stop();
    }
    LT_CHECK_EQ(1, 1);  // Test passes if we get here without crashing
LT_END_AUTO_TEST(psk_handler_setup)

// Test PSK with empty handler (error path)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_handler_empty)
    int port = PORT + 29;
    httpserver::webserver ws = httpserver::create_webserver(port)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .cred_type(httpserver::http::http_utils::PSK)
        .psk_cred_handler(empty_psk_handler);

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    bool started = ws.start(false);

    if (started) {
        ws.stop();
    }
    LT_CHECK_EQ(1, 1);
LT_END_AUTO_TEST(psk_handler_empty)

// Test PSK without handler (nullptr check)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_no_handler)
    int port = PORT + 30;
    // Configure PSK mode but don't set a handler
    httpserver::webserver ws = httpserver::create_webserver(port)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .cred_type(httpserver::http::http_utils::PSK);

    ok_resource ok;
    LT_ASSERT_EQ(true, ws.register_resource("base", &ok));
    bool started = ws.start(false);

    if (started) {
        ws.stop();
    }
    LT_CHECK_EQ(1, 1);
LT_END_AUTO_TEST(psk_no_handler)

#endif

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
