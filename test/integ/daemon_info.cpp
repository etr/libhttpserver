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
    webserver ws = create_webserver(PORT);

    simple_resource sr;
    ws.register_resource("test", &sr);
    ws.start(false);

    LT_CHECK_EQ(ws.get_bound_port(), PORT);
    LT_CHECK_GT(ws.get_listen_fd(), 0);
    LT_CHECK_EQ(ws.get_active_connections(), 0u);

    ws.stop();
LT_END_AUTO_TEST(get_bound_port_explicit)

LT_BEGIN_AUTO_TEST(daemon_info_suite, basic_request_succeeds)
    webserver ws = create_webserver(PORT);

    simple_resource sr;
    ws.register_resource("test", &sr);
    ws.start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_perform(curl);
    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    ws.stop();
LT_END_AUTO_TEST(basic_request_succeeds)

LT_BEGIN_AUTO_TEST(daemon_info_suite, quiesce_does_not_crash)
    webserver ws = create_webserver(PORT);

    simple_resource sr;
    ws.register_resource("test", &sr);
    ws.start(false);

    // Verify it works before quiesce
    curl_global_init(CURL_GLOBAL_ALL);
    string s;
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/test");
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
LT_END_AUTO_TEST(quiesce_does_not_crash)

LT_BEGIN_AUTO_TEST(daemon_info_suite, utility_functions)
    const char* version = httpserver::http::http_utils::get_mhd_version();
    LT_CHECK_NEQ(version, nullptr);

    const char* phrase = httpserver::http::http_utils::reason_phrase(200);
    LT_CHECK_EQ(string(phrase), "OK");

    const char* not_found = httpserver::http::http_utils::reason_phrase(404);
    LT_CHECK_EQ(string(not_found), "Not Found");
LT_END_AUTO_TEST(utility_functions)

LT_BEGIN_AUTO_TEST(daemon_info_suite, is_feature_supported_check)
    // MHD_FEATURE_MESSAGES is universally supported (basic error logging)
    LT_CHECK_EQ(httpserver::http::http_utils::is_feature_supported(MHD_FEATURE_MESSAGES), true);

    // MHD_FEATURE_LARGE_FILE support depends on platform/build configuration.
    // Just verify the call does not crash.
    bool large_file = httpserver::http::http_utils::is_feature_supported(MHD_FEATURE_LARGE_FILE);
    (void)large_file;

    // MHD_FEATURE_AUTODETECT_BIND_PORT support depends on the platform.
    // Just verify the call does not crash.
    bool autodetect = httpserver::http::http_utils::is_feature_supported(MHD_FEATURE_AUTODETECT_BIND_PORT);
    (void)autodetect;
LT_END_AUTO_TEST(is_feature_supported_check)

// Drive the MHD external event loop alongside a curl multi handle.
// Returns true if curl completed within max_iters iterations.
static bool drive_event_loop(webserver& ws, CURLM* multi, int max_iters) {
    int still_running = 1;
    while (still_running && max_iters-- > 0) {
        // Let MHD process with a short timeout
        ws.run_wait(50);

        // Let curl process
        curl_multi_perform(multi, &still_running);
    }
    return (still_running == 0);
}

LT_BEGIN_AUTO_TEST(daemon_info_suite, external_event_loop)
    // Start server in external event loop mode (no internal threading)
    webserver ws = create_webserver(PORT)
        .start_method(httpserver::http::http_utils::EXTERNAL_SELECT)
        .no_thread_safety();

    simple_resource sr;
    ws.register_resource("test", &sr);
    ws.start(false);

    // Drive one request through the event loop manually
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    string s;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:" PORT_STRING "/test");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    // Use run_wait to drive the event loop - it blocks until activity
    // or timeout. We use a non-blocking curl multi handle to send
    // a request while driving the MHD event loop.
    CURLM *multi = curl_multi_init();
    curl_multi_add_handle(multi, curl);

    bool completed = drive_event_loop(ws, multi, 200);
    LT_CHECK_EQ(completed, true);

    long http_code = 0;  // NOLINT(runtime/int)
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    LT_CHECK_EQ(http_code, 200);
    LT_CHECK_EQ(s, "OK");

    curl_multi_remove_handle(multi, curl);
    curl_easy_cleanup(curl);
    curl_multi_cleanup(multi);
    curl_global_cleanup();

    ws.stop();
LT_END_AUTO_TEST(external_event_loop)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
