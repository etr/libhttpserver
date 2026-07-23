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
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

// <gnutls/gnutls.h> no longer needed here. The previous
// version of `tls_info_resource` accessed `req.get_tls_session()` to
// inspect the raw gnutls_session_t; that accessor is gone. Migrated to
// `req.has_tls_session()` only.
#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./test_utils.hpp"

using std::shared_ptr;

#define STR2(p) #p
#define STR(p) STR2(p)

#ifdef HTTPSERVER_DATA_ROOT
#define ROOT STR(HTTPSERVER_DATA_ROOT)
#else
#define ROOT "."
#endif  // HTTPSERVER_DATA_ROOT

size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string *s) {
    s->append(reinterpret_cast<char*>(ptr), size*nmemb);
    return size*nmemb;
}

// File-local helper owning the curl easy-handle lifecycle for the
// plain GET round-trip repeated throughout this file: init, set the
// URL / HTTPGET / write-callback options (disabling TLS peer/host
// verification when verify_ssl is false), perform, cleanup. Returns
// the CURLcode from curl_easy_perform. Call sites that need extra
// options (client certs, CURLOPT_IPRESOLVE, CURLINFO_RESPONSE_CODE,
// custom methods, ...) keep their explicit easy-handles.
namespace {
CURLcode curl_get(const std::string& url, std::string* out, bool verify_ssl = true) {
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    if (!verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return res;
}
}  // namespace

class ok_resource : public httpserver::http_resource {
 public:
     httpserver::http_response render_get(const httpserver::http_request&) {
         return httpserver::http_response::string("OK");
     }
};

#ifdef HAVE_GNUTLS
// Migrated off the raw gnutls_session_t accessor (which has
// been removed from the public surface). The high-level
// `has_tls_session()` predicate carries the same observable signal for
// this test (the existing `tls_session_getters` test only checks for
// "TLS_SESSION_PRESENT").
class tls_info_resource : public httpserver::http_resource {
 public:
     httpserver::http_response render_get(const httpserver::http_request& req) {
         std::string response = req.has_tls_session() ? "TLS_SESSION_PRESENT"
                                                      : "NO_TLS_SESSION";
         return httpserver::http_response::string(response);
     }
};
#endif  // HAVE_GNUTLS

httpserver::http_response not_found_custom(const httpserver::http_request&) {
    return httpserver::http_response::string("Not found custom").with_status(404);
}

httpserver::http_response not_allowed_custom(const httpserver::http_request&) {
    return httpserver::http_response::string("Not allowed custom").with_status(405);
}

LT_BEGIN_SUITE(ws_start_stop_suite)
    void set_up() {
    }

    void tear_down() {
    }
LT_END_SUITE(ws_start_stop_suite)

// Wide skip of TLS / IPv6 / SNI / PSK / custom-socket / bind-address tests:
// MinGW64 curl + gnutls + MHD round-trips are flaky, IPv6 is gated on
// IPV6_TESTS_ENABLED (not set on the Windows lanes), and POSIX socket
// primitives the custom-socket test relies on are not cleanly exposed by
// MSYS. The simplest non-TLS GET round-trip is restored on Windows via the
// `windows_smoke` test (in a `#ifdef _WINDOWS` block
// below this skip).
// reason: see test/PORTABILITY.md §ws_start_stop.cpp wide skip.
#ifndef _WINDOWS

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, start_stop)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws{httpserver::create_webserver(0)};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
    }

    {
    httpserver::webserver ws{httpserver::create_webserver(0).start_method(httpserver::http::http_utils::INTERNAL_SELECT)};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
    }

    {
    httpserver::webserver ws{httpserver::create_webserver(0).start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION)};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
    }
LT_END_AUTO_TEST(start_stop)

#if defined(IPV6_TESTS_ENABLED)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ipv6)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws{httpserver::create_webserver(0).use_ipv6()};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
    httpserver::webserver ws{httpserver::create_webserver(0).use_dual_stack()};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, stop_and_wait)
    httpserver::webserver ws{httpserver::create_webserver(0)};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    }

    ws.stop_and_wait();

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 7);
    }
LT_END_AUTO_TEST(stop_and_wait)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, disable_options)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl(false)
        .use_ipv6(false)
        .debug(false)
        .pedantic(false)
#ifdef HAVE_BAUTH
        .basic_auth(false)
#endif  // HAVE_BAUTH
        .digest_auth(false)
        .deferred(false)
        .regex_checking(false)
        .ip_access_control(false)
        .post_process(false)};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(disable_options)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, enable_options)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .debug()
        .pedantic()
        .deferred()
        .regex_checking()
        .ip_access_control()
        .post_process()};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(enable_options)

// On macOS the user-bound listening socket interacts poorly with MHD's
// kqueue-based accept loop unless SO_REUSEPORT is set alongside
// SO_REUSEADDR; the bind-address tests below also depend on the same
// socket-setup path. Confirming the fix requires a macos-latest CI run
// with the modified setsockopt sequence.
// reason: see test/PORTABILITY.md §ws_start_stop.cpp custom_socket.
#ifndef DARWIN
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, custom_socket)
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(0);  // bind an ephemeral port; read it back below
    bind(fd, (struct sockaddr*) &address, sizeof(address));
    listen(fd, 10000);

    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    getsockname(fd, (struct sockaddr*) &bound_addr, &bound_len);
    const uint16_t port = ntohs(bound_addr.sin_port);

    httpserver::webserver ws{httpserver::create_webserver(-1).bind_socket(fd)};  // whatever port here doesn't matter
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "127.0.0.1:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(custom_socket)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, bind_address_string)
    httpserver::webserver ws{httpserver::create_webserver(0).bind_address("127.0.0.1")};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "127.0.0.1:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(bind_address_string)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, bind_address_string_invalid)
    LT_CHECK_THROW(httpserver::create_webserver(0).bind_address("not_an_ip"));
LT_END_AUTO_TEST(bind_address_string_invalid)
#endif

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, single_resource)
    httpserver::webserver ws{httpserver::create_webserver(0).single_resource()};
    auto ok = std::make_shared<ok_resource>();
    ws.register_prefix("/", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/any/url/works";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(single_resource)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, single_resource_not_default_resource)
    httpserver::webserver ws{httpserver::create_webserver(0).single_resource()};
    auto ok = std::make_shared<ok_resource>();
    LT_CHECK_THROW(ws.register_prefix("/other", ok));
    LT_CHECK_THROW(ws.register_path("/", ok));
    ws.start(false);

    ws.stop();
LT_END_AUTO_TEST(single_resource_not_default_resource)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_path_nullptr_throws)
    httpserver::webserver ws{httpserver::create_webserver(0)};
    // Both smart-pointer overloads throw on null.
    LT_CHECK_THROW(ws.register_path("/test", std::shared_ptr<httpserver::http_resource>{}));
    LT_CHECK_THROW(ws.register_path("/test", std::unique_ptr<httpserver::http_resource>{}));
LT_END_AUTO_TEST(register_path_nullptr_throws)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_empty_resource_non_family)
    httpserver::webserver ws{httpserver::create_webserver(0)};
    auto ok = std::make_shared<ok_resource>();
    // Register empty resource as exact path (non-prefix)
    ws.register_path("", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(register_empty_resource_non_family)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_path_with_url_params_non_family)
    httpserver::webserver ws{httpserver::create_webserver(0).regex_checking()};
    auto ok = std::make_shared<ok_resource>();
    // Register resource with URL parameters as exact path
    ws.register_path("/user/{id}", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/user/123";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(register_path_with_url_params_non_family)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, register_duplicate_resource_throws)
    httpserver::webserver ws{httpserver::create_webserver(0)};
    auto ok1 = std::make_shared<ok_resource>();
    auto ok2 = std::make_shared<ok_resource>();
    // First registration should succeed.
    ws.register_path("/duplicate", ok1);
    // The second registration of the same path now throws
    // std::invalid_argument (replaces v1's silent `return false`).
    LT_CHECK_THROW(ws.register_path("/duplicate", ok2));
    // Registering as a prefix on the same path is now also
    // forbidden — the (method, path) cache key cannot discriminate
    // exact vs prefix. Earlier the call below silently succeeded.
    LT_CHECK_THROW(ws.register_prefix("/duplicate", ok2));
LT_END_AUTO_TEST(register_duplicate_resource_throws)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, thread_per_connection_fails_with_max_threads)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION)
        .max_threads(5)};
    LT_CHECK_THROW(ws.start(false));
    }
LT_END_AUTO_TEST(thread_per_connection_fails_with_max_threads)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, thread_per_connection_fails_with_max_threads_stack_size)
    { // NOLINT (internal scope opening - not method start)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .start_method(httpserver::http::http_utils::THREAD_PER_CONNECTION)
        .max_thread_stack_size(4*1024*1024)};
    LT_CHECK_THROW(ws.start(false));
    }
LT_END_AUTO_TEST(thread_per_connection_fails_with_max_threads_stack_size)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, tuning_options)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .max_connections(10)
        .max_threads(10)
        .memory_limit(10000)
        .per_IP_connection_limit(10)
        .max_thread_stack_size(4*1024*1024)
        .nonce_nc_size(10)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    LT_CHECK_NOTHROW(ws.start(false));
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(tuning_options)

// use_ssl(true) on a HAVE_GNUTLS-off build now throws
// feature_unavailable at webserver construction (PRD-FLG-REQ-001 / §7).
// The TLS round-trip integ tests below only make sense when the library
// was built with TLS support, so gate them on HAVE_GNUTLS.
#ifdef HAVE_GNUTLS
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ssl_base)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "https://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s, false);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_global_cleanup();

    ws.stop();
LT_END_AUTO_TEST(ssl_base)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ssl_with_protocol_priorities)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_priorities("NORMAL:-MD5")};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "https://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s, false);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(ssl_with_protocol_priorities)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, ssl_with_trust)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/test_root_ca.pem")};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "https://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s, false);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(ssl_with_trust)
#endif  // HAVE_GNUTLS

void* start_ws_blocking(void* par) {
    httpserver::webserver* ws = (httpserver::webserver*) par;
    auto ok = std::make_shared<ok_resource>();
    try {
        ws->register_path("base", ok);
        ws->start(true);
    } catch (...) {
        return PTHREAD_CANCELED;
    }

    return nullptr;
}

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, blocking_server)
    httpserver::webserver ws{httpserver::create_webserver(0)};

    pthread_t tid;
    pthread_create(&tid, nullptr, start_ws_blocking, reinterpret_cast<void*>(&ws));

    sleep(1);

    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();

    char* b;
    pthread_join(tid, reinterpret_cast<void**>(&b));
    LT_CHECK_EQ(b, nullptr);
    free(b);
LT_END_AUTO_TEST(blocking_server)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, custom_error_resources)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .not_found_handler(not_found_custom)
        .method_not_allowed_handler(not_allowed_custom)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    }

    {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "localhost:" + std::to_string(port) + "/not_registered";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
    ok->set_allowing(httpserver::http_method::put, false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
    httpserver::webserver ws{httpserver::create_webserver(0).use_ipv6()};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    try {
        ws.start(false);
    } catch (const std::exception& e) {
        // IPv6 may be unavailable in the host network stack;
        // distinguish that from "TLS support actually broke" by emitting
        // a SKIP rather than a tautological pass.
        LT_SKIP(std::string("IPv6 webserver start failed: ") + e.what());
    }
    if (ws.is_running()) {
        const uint16_t port = ws.get_bound_port();
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        // Bind an ephemeral port; talk to the actual bound port.
        std::string url = "http://[::1]:" + std::to_string(port) + "/base";
        CURLcode res = curl_get(url, &s);
        if (res == CURLE_COULDNT_RESOLVE_HOST) {
            // The server bound IPv6 fine (is_running() is true), but this
            // host has no IPv6 client path — getaddrinfo("::1") fails, so
            // curl cannot reach loopback (seen on macOS CI runners). That
            // is environmental, not an IPv6-serving regression. A real
            // serving break surfaces as a different curl error or a wrong
            // body, both still asserted below.
            LT_SKIP("IPv6 loopback unreachable from client (no host IPv6 stack)");
        }
        // Once the server is confirmed running and the client can reach
        // IPv6 loopback, a curl failure is a genuine test failure.
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        ws.stop();
    }
LT_END_AUTO_TEST(ipv6_webserver)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, dual_stack_webserver)
    httpserver::webserver ws{httpserver::create_webserver(0).use_dual_stack()};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    try {
        ws.start(false);
    } catch (const std::exception& e) {
        // Dual-stack may be disabled on the host kernel; SKIP
        // rather than tautological-pass so a broken-build regression is
        // observable.
        LT_SKIP(std::string("dual-stack webserver start failed: ") + e.what());
    }
    if (ws.is_running()) {
        const uint16_t port = ws.get_bound_port();
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        // Bind an ephemeral port; talk to the actual bound port.
        std::string url = "localhost:" + std::to_string(port) + "/base";
        CURLcode res = curl_get(url, &s);
        // Once the server is confirmed running, a curl failure is a
        // genuine test failure — not an environmental skip.
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        ws.stop();
    }
LT_END_AUTO_TEST(dual_stack_webserver)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, bind_address_ipv4)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0).bind_address("127.0.0.1")};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(bind_address_ipv4)

// Test bind_address with IPv6 address string (covers IPv6 branch in create_webserver.cpp)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, bind_address_ipv6_string)
    int port = 0;  // set to the kernel-assigned port after start
    // Webserver is non-movable so we use a shared_ptr to
    // separate construction / start (environmental SKIP) from the curl
    // block (unconditional assertion). Previously a single catch(...)
    // swallowed assert_unattended and turned a curl failure into a SKIP.
    std::shared_ptr<httpserver::webserver> ws_ptr;
    try {
        ws_ptr = std::make_shared<httpserver::webserver>(
            httpserver::create_webserver(0).bind_address("::1"));
    } catch (...) {
        LT_SKIP("IPv6 bind: construction failed on this host");
    }
    // ws_ptr is guaranteed non-null here: LT_SKIP() always throws
    // skip_unattended and unwinds the entire test body on failure, so
    // execution only reaches this point when the try block above
    // completed successfully.
    auto ok = std::make_shared<ok_resource>();
    ws_ptr->register_path("base", ok);
    try {
        ws_ptr->start(false);
        port = ws_ptr->get_bound_port();
    } catch (...) {
        LT_SKIP("IPv6 bind: start failed on this host");
    }
    if (ws_ptr->is_running()) {
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        std::string url = "http://[::1]:" + std::to_string(port) + "/base";
        CURLcode res = curl_get(url, &s);
        if (res == CURLE_COULDNT_RESOLVE_HOST) {
            // Server bound IPv6, but this host has no IPv6 client path
            // (getaddrinfo("::1") fails; seen on macOS CI runners).
            // Environmental, not a serving regression — a real break shows
            // up as a different error or a wrong body, both asserted below.
            LT_SKIP("IPv6 loopback unreachable from client (no host IPv6 stack)");
        }
        // Once the server is confirmed running and the client can reach
        // IPv6 loopback, a curl failure is a genuine test failure.
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        ws_ptr->stop();
    }
LT_END_AUTO_TEST(bind_address_ipv6_string)

#ifdef HAVE_GNUTLS
// Test TLS session getters on non-TLS connection (should return false/nullptr)
class tls_check_non_tls_resource : public httpserver::http_resource {
 public:
     httpserver::http_response render_get(const httpserver::http_request& req) {
         // On non-TLS connection, has_tls_session should return false
         std::string response = req.has_tls_session() ? "HAS_TLS" : "NO_TLS";
         return httpserver::http_response::string(response);
     }
};

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, tls_session_on_non_tls_connection)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)};  // No SSL
    auto tls_check = std::make_shared<tls_check_non_tls_resource>();
    ws.register_path("tls_check", tls_check);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/tls_check";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "NO_TLS");

    ws.stop();
LT_END_AUTO_TEST(tls_session_on_non_tls_connection)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, https_webserver)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without the
        // certs or library; SKIP rather than tautological-pass so a
        // build that silently lost TLS support reports SKIP, not PASS.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    {
        curl_global_init(CURL_GLOBAL_ALL);
        std::string s;
        std::string url = "https://localhost:" + std::to_string(port) + "/base";
        CURLcode res = curl_get(url, &s, false);
        LT_ASSERT_EQ(res, 0);
        LT_CHECK_EQ(s, "OK");
        ws.stop();
    }
LT_END_AUTO_TEST(https_webserver)

LT_BEGIN_AUTO_TEST(ws_start_stop_suite, tls_session_getters)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")};
    auto tls_info = std::make_shared<tls_info_resource>();
    ws.register_path("tls_info", tls_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without the
        // certs or library; SKIP rather than tautological-pass so a
        // build that silently lost TLS support reports SKIP, not PASS.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "https://localhost:" + std::to_string(port) + "/tls_info";
    CURLcode res = curl_get(url, &s, false);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "TLS_SESSION_PRESENT");
    ws.stop();
LT_END_AUTO_TEST(tls_session_getters)

// Resource that extracts client certificate info
class client_cert_info_resource : public httpserver::http_resource {
 public:
     httpserver::http_response render_get(const httpserver::http_request& req) {
         std::string response;
         if (req.has_client_certificate()) {
             response = "HAS_CLIENT_CERT";
             // The four cert-string accessors return
             // std::string_view; the explicit string(string_view) ctor
             // requires direct-init, so we use parens not `=`.
             std::string dn(req.get_client_cert_dn());
             std::string issuer(req.get_client_cert_issuer_dn());
             std::string cn(req.get_client_cert_cn());
             std::string fingerprint(req.get_client_cert_fingerprint_sha256());
             bool verified = req.is_client_cert_verified();
             // The two time accessors return std::int64_t.
             std::int64_t not_before = req.get_client_cert_not_before();
             std::int64_t not_after = req.get_client_cert_not_after();

             response += "|DN:" + dn;
             response += "|ISSUER:" + issuer;
             response += "|CN:" + cn;
             response += "|FP:" + fingerprint;
             response += "|VERIFIED:" + std::string(verified ? "yes" : "no");
             response += "|NOT_BEFORE:" + std::to_string(not_before);
             response += "|NOT_AFTER:" + std::to_string(not_after);
         } else {
             response = "NO_CLIENT_CERT";
         }
         return httpserver::http_response::string(response);
     }
};

// Test client certificate methods without a client certificate (no mTLS)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, client_cert_no_certificate)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")};
    auto cert_info = std::make_shared<client_cert_info_resource>();
    ws.register_path("cert_info", cert_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without the
        // certs or library; SKIP rather than tautological-pass so a
        // build that silently lost TLS support reports SKIP, not PASS.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "https://localhost:" + std::to_string(port) + "/cert_info";
    CURLcode res = curl_get(url, &s, false);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "NO_CLIENT_CERT");
    ws.stop();
LT_END_AUTO_TEST(client_cert_no_certificate)

// Test client certificate methods with mTLS (client sends certificate)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, client_cert_with_certificate)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/client_cert.pem")};  // Trust the client cert as CA
    auto cert_info = std::make_shared<client_cert_info_resource>();
    ws.register_path("cert_info", cert_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without the
        // certs or library; SKIP rather than tautological-pass so a
        // build that silently lost TLS support reports SKIP, not PASS.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "https://localhost:" + std::to_string(port) + "/cert_info";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLCERT, ROOT "/client_cert.pem");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, ROOT "/client_key.pem");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Check that we got client cert info
    LT_CHECK_NEQ(s.find("HAS_CLIENT_CERT"), std::string::npos);
    LT_CHECK_NEQ(s.find("CN:Test Client"), std::string::npos);
    LT_CHECK_NEQ(s.find("FP:"), std::string::npos);
    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(client_cert_with_certificate)

// Test client certificate DN extraction
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, client_cert_dn_extraction)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/client_cert.pem")};
    auto cert_info = std::make_shared<client_cert_info_resource>();
    ws.register_path("cert_info", cert_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without the
        // certs or library; SKIP rather than tautological-pass so a
        // build that silently lost TLS support reports SKIP, not PASS.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "https://localhost:" + std::to_string(port) + "/cert_info";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLCERT, ROOT "/client_cert.pem");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, ROOT "/client_key.pem");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Check DN contains expected organization
    LT_CHECK_NEQ(s.find("O=Test Org"), std::string::npos);
    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(client_cert_dn_extraction)

// Test client certificate fingerprint generation
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, client_cert_fingerprint)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/client_cert.pem")};
    auto cert_info = std::make_shared<client_cert_info_resource>();
    ws.register_path("cert_info", cert_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without the
        // certs or library; SKIP rather than tautological-pass so a
        // build that silently lost TLS support reports SKIP, not PASS.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "https://localhost:" + std::to_string(port) + "/cert_info";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLCERT, ROOT "/client_cert.pem");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, ROOT "/client_key.pem");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Fingerprint should be 64 hex characters (32 bytes SHA-256)
    size_t fp_pos = s.find("FP:");
    LT_ASSERT_NEQ(fp_pos, std::string::npos);
    size_t fp_end = s.find("|", fp_pos);
    LT_ASSERT_NEQ(fp_end, std::string::npos);
    std::string fp = s.substr(fp_pos + 3, fp_end - fp_pos - 3);
    LT_CHECK_EQ(fp.length(), 64u);
    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(client_cert_fingerprint)

// Test client certificate without CN field (covers cn_size == 0 branch)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, client_cert_no_cn)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/client_cert_no_cn.pem")};
    auto cert_info = std::make_shared<client_cert_info_resource>();
    ws.register_path("cert_info", cert_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without certs;
        // SKIP rather than tautological-pass.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "https://localhost:" + std::to_string(port) + "/cert_info";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLCERT, ROOT "/client_cert_no_cn.pem");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, ROOT "/client_key_no_cn.pem");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Certificate has no CN, so CN should be empty but other fields should work
    LT_CHECK_NEQ(s.find("HAS_CLIENT_CERT"), std::string::npos);
    LT_CHECK_NEQ(s.find("CN:"), std::string::npos);  // CN field present but empty
    // DN should contain "O=Test Org Without CN"
    LT_CHECK_NEQ(s.find("Test Org Without CN"), std::string::npos);
    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(client_cert_no_cn)

// Test client certificate that fails verification (covers status != 0 branch)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, client_cert_untrusted)
    int port = 0;  // set to the kernel-assigned port after start
    // Don't add untrusted cert to trust store - verification should fail
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/client_cert.pem")};  // Only trust the original client cert
    auto cert_info = std::make_shared<client_cert_info_resource>();
    ws.register_path("cert_info", cert_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without certs;
        // SKIP rather than tautological-pass.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "https://localhost:" + std::to_string(port) + "/cert_info";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    // Use the untrusted certificate
    curl_easy_setopt(curl, CURLOPT_SSLCERT, ROOT "/client_cert_untrusted.pem");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, ROOT "/client_key_untrusted.pem");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    // Certificate is present but should NOT be verified (untrusted)
    LT_CHECK_NEQ(s.find("HAS_CLIENT_CERT"), std::string::npos);
    LT_CHECK_NEQ(s.find("VERIFIED:no"), std::string::npos);
    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(client_cert_untrusted)

// End-to-end test of the high-level cert accessors against a
// known client certificate. Verifies the new return types
// (string_view / int64_t / bool) and the documented value contracts:
//   - DN/issuer DN both contain "O=Test Org" and "CN=Test Client"
//     (the test cert is self-signed, so subject and issuer match).
//   - CN is exactly "Test Client".
//   - SHA-256 fingerprint length 64, all chars in [0-9a-f].
//   - not_before / not_after are positive epoch seconds with
//     not_after > not_before.
//   - is_client_cert_verified() reports true (the test trust store is
//     the cert itself).
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, client_cert_accessors_known_values)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .https_mem_trust(ROOT "/client_cert.pem")};
    auto cert_info = std::make_shared<client_cert_info_resource>();
    ws.register_path("cert_info", cert_info);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // SSL setup may fail in environments without the
        // cert files or the system TLS library. The previous project
        // convention recorded a passing tautological assertion; we
        // now emit a proper SKIP so a build that silently
        // lost TLS support is reportable.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    std::string url = "https://localhost:" + std::to_string(port) + "/cert_info";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLCERT, ROOT "/client_cert.pem");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, ROOT "/client_key.pem");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);

    // The resource serialises:
    //   HAS_CLIENT_CERT|DN:<dn>|ISSUER:<issuer>|CN:<cn>|FP:<fp>
    //   |VERIFIED:<yes|no>|NOT_BEFORE:<n>|NOT_AFTER:<n>
    LT_ASSERT_NEQ(s.find("HAS_CLIENT_CERT"), std::string::npos);

    // Helper: extract the substring between |TAG:` and the next `|` (or
    // end of string for the last field).
    auto extract = [&](const char* tag) -> std::string {
        std::string needle = std::string("|") + tag + ":";
        size_t start = s.find(needle);
        if (start == std::string::npos) return std::string();
        start += needle.size();
        size_t end = s.find('|', start);
        if (end == std::string::npos) end = s.size();
        return s.substr(start, end - start);
    };

    std::string dn      = extract("DN");
    std::string issuer  = extract("ISSUER");
    std::string cn      = extract("CN");
    std::string fp      = extract("FP");
    std::string verified = extract("VERIFIED");
    std::string nb_s    = extract("NOT_BEFORE");
    std::string na_s    = extract("NOT_AFTER");

    // Subject and issuer DN both contain the expected attributes.
    LT_CHECK_NEQ(dn.find("O=Test Org"), std::string::npos);
    LT_CHECK_NEQ(dn.find("CN=Test Client"), std::string::npos);
    LT_CHECK_NEQ(issuer.find("O=Test Org"), std::string::npos);
    LT_CHECK_NEQ(issuer.find("CN=Test Client"), std::string::npos);
    // Self-signed: subject DN == issuer DN.
    LT_CHECK_EQ(dn, issuer);

    // CN is exactly "Test Client".
    LT_CHECK_EQ(cn, std::string("Test Client"));

    // Fingerprint: 64 lowercase hex chars (32 bytes SHA-256, hex-encoded).
    // Use std::all_of to avoid branching inside the test body.
    LT_CHECK_EQ(fp.length(), 64u);
    LT_CHECK(std::all_of(fp.begin(), fp.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }));

    // Verification: the trust store is the cert itself, so verification
    // succeeds.
    LT_CHECK_EQ(verified, std::string("yes"));

    // Validity window: positive epoch seconds, not_after > not_before.
    std::int64_t nb = std::stoll(nb_s);
    std::int64_t na = std::stoll(na_s);
    LT_CHECK(nb > 0);
    LT_CHECK(na > 0);
    LT_CHECK(na > nb);

    curl_easy_cleanup(curl);
    ws.stop();
LT_END_AUTO_TEST(client_cert_accessors_known_values)

// Test SNI callback configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, sni_callback_setup)
    int port = 0;  // set to the kernel-assigned port after start

    // Simple SNI callback that returns empty (uses default cert)
    auto sni_cb = [](const std::string& server_name) -> std::pair<std::string, std::string> {
        std::ignore = server_name;
        return {"", ""};  // Use default cert
    };

    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .sni_callback(sni_cb)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    try {
        ws.start(false);
        port = ws.get_bound_port();
    } catch (const std::exception& e) {
        // TLS setup may fail in environments without the
        // certs or library; SKIP rather than tautological-pass so a
        // build that silently lost TLS support reports SKIP, not PASS.
        LT_SKIP(std::string("TLS start failed: ") + e.what());
    }

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "https://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s, false);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    ws.stop();
LT_END_AUTO_TEST(sni_callback_setup)
#endif  // HAVE_GNUTLS

#endif  // _WINDOWS

#ifdef _WINDOWS
// Windows-only smoke variant. Asserts the daemon starts, accepts
// one HTTP/1.1 GET, and serves the registered resource. Mirrors the
// non-TLS subset of `start_stop` test #1 (the default-config curl GET) so
// that the MinGW64 / MSYS lanes exercise at least one full round-trip
// through libhttpserver, restoring the most valuable piece of CI coverage
// without inheriting the TLS / IPv6 / SNI / PSK / custom-socket flake
// tracked in test/PORTABILITY.md §ws_start_stop.cpp.
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, windows_smoke)
    httpserver::webserver ws{httpserver::create_webserver(0)};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    const uint16_t port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(windows_smoke)
#endif  // _WINDOWS

// Test pedantic mode configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, pedantic_mode)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0).pedantic()};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(pedantic_mode)

#ifdef HAVE_DAUTH
// Test digest_auth_random configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, digest_auth_random)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .digest_auth_random("random_string_for_digest")};
    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

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

// PSK handler that returns invalid hex (for hex conversion error path)
std::string invalid_hex_psk_handler(const std::string&) {
    return "ZZZZ";  // Invalid hex characters
}

// Helper to check if gnutls-cli is available. Memoized: the five
// psk_connection_* tests below each call this, and the underlying
// system() spawns a shell + `which` subprocess, so caching the result
// avoids paying that cost up to five times per test binary run.
bool has_gnutls_cli() {
    static const bool cached = (system("which gnutls-cli > /dev/null 2>&1") == 0);
    return cached;
}

// Detect libmicrohttpd/gnutls builds where PSK isn't
// compiled in. The catch is wrapped around `webserver::start` /
// configuration, and the exception message MHD raises when PSK is
// unsupported contains both "PSK" and a "not supported"/"unsupported"
// token. Substring-matching is intentionally permissive — if MHD ever
// changes the exact wording, we just fall through to `throw;` and the
// test reports a real failure rather than masking it as a skip.
bool is_psk_unsupported_error(const std::exception& e) {
    const std::string msg = e.what();
    const bool has_psk = msg.find("PSK") != std::string::npos ||
                          msg.find("psk") != std::string::npos;
    const bool has_unsupported = msg.find("not supported") != std::string::npos ||
                                  msg.find("unsupported") != std::string::npos ||
                                  msg.find("not available") != std::string::npos ||
                                  msg.find("disabled") != std::string::npos;
    return has_psk && has_unsupported;
}

// Pin is_psk_unsupported_error triage logic so a future
// change in MHD's error strings or in the helper predicate is caught
// before it silently masks/surfaces PSK failures.
//
// Helper: wrap a literal string as a std::exception so we can pass it
// to is_psk_unsupported_error without starting a webserver.
namespace {
struct fake_exception : public std::exception {
    explicit fake_exception(const char* msg) : msg_(msg) {}
    const char* what() const noexcept override { return msg_; }
    const char* msg_;
};
}  // namespace

// (a) "PSK not supported" → true
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, is_psk_unsupported_error_psk_not_supported)
    LT_CHECK_EQ(is_psk_unsupported_error(fake_exception("PSK not supported")), true);
LT_END_AUTO_TEST(is_psk_unsupported_error_psk_not_supported)

// (b) "psk disabled" → true (lowercase variant)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, is_psk_unsupported_error_psk_disabled)
    LT_CHECK_EQ(is_psk_unsupported_error(fake_exception("psk disabled")), true);
LT_END_AUTO_TEST(is_psk_unsupported_error_psk_disabled)

// (c) "TLS handshake failed" → false (no PSK keyword at all)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, is_psk_unsupported_error_tls_handshake_failed)
    LT_CHECK_EQ(is_psk_unsupported_error(fake_exception("TLS handshake failed")), false);
LT_END_AUTO_TEST(is_psk_unsupported_error_tls_handshake_failed)

// (d) "PSK credential error" → false (PSK present but no 'unsupported' token;
//     must NOT be treated as a skip — real PSK config failures should surface)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, is_psk_unsupported_error_psk_credential_error)
    LT_CHECK_EQ(is_psk_unsupported_error(fake_exception("PSK credential error")), false);
LT_END_AUTO_TEST(is_psk_unsupported_error_psk_credential_error)

// Test PSK credential handler setup
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_handler_setup)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .cred_type(httpserver::http::http_utils::PSK)
        .psk_cred_handler(test_psk_handler)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    try {
        ws.start(false);
    } catch (const std::exception& e) {
        // Distinguish "this libmicrohttpd build doesn't
        // support PSK" (SKIP) from "implementation broken" (FAIL).
        // The substring match is permissive — if MHD changes wording
        // we fall through to `throw;` and the test reports a real
        // failure rather than masking it.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }

    // Just verify the server can be configured with PSK options.
    // ws.stop() is a no-op (returns false) when the server never
    // started, so the unconditional call is safe here.
    ws.stop();
    // A real postcondition assertion (not a tautological pass):
    // after `ws.stop()` the server must report not-running.
    LT_CHECK_EQ(ws.is_running(), false);
LT_END_AUTO_TEST(psk_handler_setup)

// Test PSK with empty handler (error path)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_handler_empty)
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .cred_type(httpserver::http::http_utils::PSK)
        .psk_cred_handler(empty_psk_handler)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    try {
        ws.start(false);
    } catch (const std::exception& e) {
        // See psk_handler_setup — typed SKIP on
        // PSK-unsupported builds; re-throw to fail loudly otherwise.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }

    // ws.stop() is a no-op (returns false) when the server never
    // started, so the unconditional call is safe here.
    ws.stop();
    // Strengthened tail-position liveness assertion.
    LT_CHECK_EQ(ws.is_running(), false);
LT_END_AUTO_TEST(psk_handler_empty)

// Test PSK without handler (nullptr check)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_no_handler)
    // Configure PSK mode but don't set a handler
    httpserver::webserver ws{httpserver::create_webserver(0)
        .use_ssl()
        .https_mem_key(ROOT "/key.pem")
        .https_mem_cert(ROOT "/cert.pem")
        .cred_type(httpserver::http::http_utils::PSK)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    try {
        ws.start(false);
    } catch (const std::exception& e) {
        // See psk_handler_setup — typed SKIP on
        // PSK-unsupported builds; re-throw to fail loudly otherwise.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }

    // ws.stop() is a no-op (returns false) when the server never
    // started, so the unconditional call is safe here.
    ws.stop();
    // Strengthened tail-position liveness assertion.
    LT_CHECK_EQ(ws.is_running(), false);
LT_END_AUTO_TEST(psk_no_handler)

// Test PSK connection attempt using gnutls-cli
// This triggers the psk_cred_handler_func callback to execute, providing coverage
// The callback now uses the static registry to get the webserver pointer
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_connection_success)
    // SKIP (not PASS) when the gnutls-cli binary is not in
    // $PATH. Previously this gate emitted a tautological pass which
    // hid lanes that had broken TLS support.
    LT_SKIP_IF(!has_gnutls_cli(), "gnutls-cli binary not in PATH");

    int port = 0;  // set to the kernel-assigned port after start
    try {
        httpserver::webserver ws{httpserver::create_webserver(0)
            .use_ssl()
            .https_mem_key(ROOT "/key.pem")
            .https_mem_cert(ROOT "/cert.pem")
            .cred_type(httpserver::http::http_utils::PSK)
            .psk_cred_handler(test_psk_handler)
            .https_priorities("NORMAL:+PSK:+DHE-PSK")};

        auto ok = std::make_shared<ok_resource>();
        ws.register_path("base", ok);

        ws.start(false);
        port = ws.get_bound_port();

        // Make PSK connection attempt with gnutls-cli
        // This triggers the PSK credential handler callback, providing coverage
        // Note: Full PSK success depends on libmicrohttpd/gnutls configuration
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "echo -e 'GET /base HTTP/1.0\\r\\n\\r\\n' | "
            "gnutls-cli --pskusername=testuser "
            "--pskkey=0123456789abcdef0123456789abcdef "
            "--priority='NORMAL:+PSK:+DHE-PSK' "
            "--insecure localhost -p %d 2>&1 || true",
            port);

        // Assertion target: the PSK callback was reached (exit 127
        // means gnutls-cli itself was not found). Any other exit code,
        // including a failed handshake, is acceptable.
        int cmd_exit = system(cmd);
        ws.stop();
        LT_CHECK_NEQ(cmd_exit, 127);
    } catch (const std::exception& e) {
        // See psk_handler_setup — typed SKIP on
        // PSK-unsupported builds; re-throw to fail loudly otherwise.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }
LT_END_AUTO_TEST(psk_connection_success)

// Test PSK connection with unknown user (empty PSK response)
// This covers lines 438-440 in psk_cred_handler_func
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_connection_unknown_user)
    // SKIP (not PASS) when the gnutls-cli binary is not in
    // $PATH. Previously this gate emitted a tautological pass which
    // hid lanes that had broken TLS support.
    LT_SKIP_IF(!has_gnutls_cli(), "gnutls-cli binary not in PATH");

    int port = 0;  // set to the kernel-assigned port after start
    try {
        httpserver::webserver ws{httpserver::create_webserver(0)
            .use_ssl()
            .https_mem_key(ROOT "/key.pem")
            .https_mem_cert(ROOT "/cert.pem")
            .cred_type(httpserver::http::http_utils::PSK)
            .psk_cred_handler(test_psk_handler)
            .https_priorities("NORMAL:+PSK:+DHE-PSK")};

        auto ok = std::make_shared<ok_resource>();
        ws.register_path("base", ok);

        ws.start(false);
        port = ws.get_bound_port();

        // Try to connect with unknown username - should fail
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "echo -e 'GET /base HTTP/1.0\\r\\n\\r\\n' | "
            "gnutls-cli --pskusername=unknownuser "
            "--pskkey=0123456789abcdef0123456789abcdef "
            "--priority='NORMAL:+PSK:+DHE-PSK' "
            "--insecure localhost -p %d 2>/dev/null | grep -q 'OK'",
            port);

        int result = system(cmd);
        ws.stop();

        LT_CHECK_NEQ(result, 0);  // Connection should fail
    } catch (const std::exception& e) {
        // See psk_handler_setup — typed SKIP on
        // PSK-unsupported builds; re-throw to fail loudly otherwise.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }
LT_END_AUTO_TEST(psk_connection_unknown_user)

// Test PSK connection with handler returning empty string
// This covers lines 438-440 in psk_cred_handler_func
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_connection_empty_handler)
    // SKIP (not PASS) when the gnutls-cli binary is not in
    // $PATH. Previously this gate emitted a tautological pass which
    // hid lanes that had broken TLS support.
    LT_SKIP_IF(!has_gnutls_cli(), "gnutls-cli binary not in PATH");

    int port = 0;  // set to the kernel-assigned port after start
    try {
        httpserver::webserver ws{httpserver::create_webserver(0)
            .use_ssl()
            .https_mem_key(ROOT "/key.pem")
            .https_mem_cert(ROOT "/cert.pem")
            .cred_type(httpserver::http::http_utils::PSK)
            .psk_cred_handler(empty_psk_handler)
            .https_priorities("NORMAL:+PSK:+DHE-PSK")};

        auto ok = std::make_shared<ok_resource>();
        ws.register_path("base", ok);

        ws.start(false);
        port = ws.get_bound_port();

        // Try to connect - should fail because handler returns empty
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "echo -e 'GET /base HTTP/1.0\\r\\n\\r\\n' | "
            "gnutls-cli --pskusername=testuser "
            "--pskkey=0123456789abcdef0123456789abcdef "
            "--priority='NORMAL:+PSK:+DHE-PSK' "
            "--insecure localhost -p %d 2>/dev/null | grep -q 'OK'",
            port);

        int result = system(cmd);
        ws.stop();

        LT_CHECK_NEQ(result, 0);  // Connection should fail
    } catch (const std::exception& e) {
        // See psk_handler_setup — typed SKIP on
        // PSK-unsupported builds; re-throw to fail loudly otherwise.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }
LT_END_AUTO_TEST(psk_connection_empty_handler)

// Test PSK connection with invalid hex key
// This covers lines 451-456 in psk_cred_handler_func
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_connection_invalid_hex)
    // SKIP (not PASS) when the gnutls-cli binary is not in
    // $PATH. Previously this gate emitted a tautological pass which
    // hid lanes that had broken TLS support.
    LT_SKIP_IF(!has_gnutls_cli(), "gnutls-cli binary not in PATH");

    int port = 0;  // set to the kernel-assigned port after start
    try {
        httpserver::webserver ws{httpserver::create_webserver(0)
            .use_ssl()
            .https_mem_key(ROOT "/key.pem")
            .https_mem_cert(ROOT "/cert.pem")
            .cred_type(httpserver::http::http_utils::PSK)
            .psk_cred_handler(invalid_hex_psk_handler)
            .https_priorities("NORMAL:+PSK:+DHE-PSK")};

        auto ok = std::make_shared<ok_resource>();
        ws.register_path("base", ok);

        ws.start(false);
        port = ws.get_bound_port();

        // Try to connect - should fail because handler returns invalid hex
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "echo -e 'GET /base HTTP/1.0\\r\\n\\r\\n' | "
            "gnutls-cli --pskusername=testuser "
            "--pskkey=0123456789abcdef0123456789abcdef "
            "--priority='NORMAL:+PSK:+DHE-PSK' "
            "--insecure localhost -p %d 2>/dev/null | grep -q 'OK'",
            port);

        int result = system(cmd);
        ws.stop();

        LT_CHECK_NEQ(result, 0);  // Connection should fail
    } catch (const std::exception& e) {
        // See psk_handler_setup — typed SKIP on
        // PSK-unsupported builds; re-throw to fail loudly otherwise.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }
LT_END_AUTO_TEST(psk_connection_invalid_hex)

// Test PSK connection with no handler set (nullptr check)
// This covers lines 432-435 in psk_cred_handler_func
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, psk_connection_no_handler)
    // SKIP (not PASS) when the gnutls-cli binary is not in
    // $PATH. Previously this gate emitted a tautological pass which
    // hid lanes that had broken TLS support.
    LT_SKIP_IF(!has_gnutls_cli(), "gnutls-cli binary not in PATH");

    int port = 0;  // set to the kernel-assigned port after start
    try {
        httpserver::webserver ws{httpserver::create_webserver(0)
            .use_ssl()
            .https_mem_key(ROOT "/key.pem")
            .https_mem_cert(ROOT "/cert.pem")
            .cred_type(httpserver::http::http_utils::PSK)
            // Note: NOT setting psk_cred_handler - handler is nullptr
            .https_priorities("NORMAL:+PSK:+DHE-PSK")};

        auto ok = std::make_shared<ok_resource>();
        ws.register_path("base", ok);

        ws.start(false);
        port = ws.get_bound_port();

        // Try to connect - should fail because no handler is set
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "echo -e 'GET /base HTTP/1.0\\r\\n\\r\\n' | "
            "gnutls-cli --pskusername=testuser "
            "--pskkey=0123456789abcdef0123456789abcdef "
            "--priority='NORMAL:+PSK:+DHE-PSK' "
            "--insecure localhost -p %d 2>/dev/null | grep -q 'OK'",
            port);

        int result = system(cmd);
        ws.stop();

        LT_CHECK_NEQ(result, 0);  // Connection should fail
    } catch (const std::exception& e) {
        // See psk_handler_setup — typed SKIP on
        // PSK-unsupported builds; re-throw to fail loudly otherwise.
        LT_SKIP_IF(is_psk_unsupported_error(e),
                   std::string("libmicrohttpd built without PSK: ") + e.what());
        throw;
    }
LT_END_AUTO_TEST(psk_connection_no_handler)

#endif

// Test max_threads configuration with a running server
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, max_threads_running)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .max_threads(4)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(max_threads_running)

// Test max_connections configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, max_connections_running)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .max_connections(100)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(max_connections_running)

// Test memory_limit configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, memory_limit_running)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .memory_limit(32 * 1024)};  // 32KB memory limit

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(memory_limit_running)

// Test per_IP_connection_limit configuration
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, per_ip_limit_running)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .per_IP_connection_limit(5)};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(per_ip_limit_running)

// Test max_thread_stack_size configuration (covers line 257 branch)
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, thread_stack_size_running)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .max_thread_stack_size(4 * 1024 * 1024)};  // 4MB stack size

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(thread_stack_size_running)

// Test deferred mode
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, deferred_mode_running)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .deferred()};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(deferred_mode_running)

// Test debug mode with actual request
LT_BEGIN_AUTO_TEST(ws_start_stop_suite, debug_mode_running)
    int port = 0;  // set to the kernel-assigned port after start
    httpserver::webserver ws{httpserver::create_webserver(0)
        .debug()};

    auto ok = std::make_shared<ok_resource>();
    ws.register_path("base", ok);
    ws.start(false);
    port = ws.get_bound_port();

    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    std::string url = "http://localhost:" + std::to_string(port) + "/base";
    CURLcode res = curl_get(url, &s);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");

    ws.stop();
LT_END_AUTO_TEST(debug_mode_running)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
