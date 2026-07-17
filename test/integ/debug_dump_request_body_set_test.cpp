/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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

// When LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY is set, the
// server MUST:
//   - write a one-shot SECURITY WARNING to stderr on the first
//     webserver::start() call (and only the first, even if multiple
//     webservers start in the same process);
//   - write each POST body chunk to stdout prefixed by
//     "Writing content:".
//
// The env var is opted in via setenv() before the test bodies run (and
// before the magic-static cache in the body pipeline is initialised on
// first dispatch). Both assertions live in a single binary (the
// magic-static body-pipeline cache means the first observation locks in
// for the rest of the process).

#include <curl/curl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./curl_helpers.hpp"
#include "./stream_capture_helpers.hpp"

using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::http_resource;
using httpserver_test::begin_capture;
using httpserver_test::end_capture;
using httpserver_test::writefunc;

namespace {

class echo_resource : public http_resource {
 public:
    http_response render(const http_request&) override {
        return http_response::string("OK");
    }
};

CURLcode run_one_post(uint16_t port, const std::string& path,
                      const std::string& body) {
    curl_global_init(CURL_GLOBAL_ALL);
    std::string resp;
    CURL* curl = curl_easy_init();
    std::string url = "http://localhost:" + std::to_string(port) + "/" + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return res;
}

}  // namespace

LT_BEGIN_SUITE(dump_set_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(dump_set_suite)

// NOTE ON TEST ORDER: this test is deliberately defined (and therefore
// runs, per littletest's definition-order registration -- see
// LT_BEGIN_TEST in littletest.hpp) BEFORE
// dump_body_to_stdout_when_env_set below. The startup-warning is a
// one-shot, process-lifetime, magic-static-gated event: whichever test
// starts a webserver first in this binary is the one that observes it.
// This test must run first so its own ws1 start is that first
// observation; if the stdout-only test ran first instead, the warning
// would already be latched by the time this test's assertions run and
// the idempotence check below would see zero occurrences instead of one.
LT_BEGIN_AUTO_TEST(dump_set_suite, startup_warning_emitted_exactly_once_across_multiple_webservers)
    auto stderr_cap = begin_capture(STDERR_FILENO, "set_stderr");

    // ---- First webserver: should warn ------------------------------------
    auto ws1 = std::make_unique<webserver>(
        create_webserver(9182)
            .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
            .max_threads(2));
    auto r1 = std::make_shared<echo_resource>();
    ws1->register_path("echo", r1);
    ws1->start(false);
    CURLcode res1 = run_one_post(9182, "echo", "secret=opted-in-body");
    ws1->stop();
    // Quiesce point for the one-warning-per-process assertion below:
    // webserver::stop() (called above and again by ~webserver via
    // reset()) invokes MHD_stop_daemon(), which blocks until all active
    // connections are drained and every libmicrohttpd worker thread has
    // been joined (see webserver::stop() in src/detail/webserver_setup.cpp
    // and the stop_and_wait() contract in src/webserver.cpp). ws1's MHD
    // threads are therefore fully gone before ws2 starts -- nothing from
    // ws1 can write to the captured stderr concurrently with ws2's
    // start(), so counting "SECURITY WARNING" occurrences across both
    // lifetimes is race-free.
    ws1.reset();

    // ---- Second webserver: warning must NOT fire again ------------------
    auto ws2 = std::make_unique<webserver>(
        create_webserver(9183)
            .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
            .max_threads(2));
    auto r2 = std::make_shared<echo_resource>();
    ws2->register_path("echo", r2);
    ws2->start(false);
    CURLcode res2 = run_one_post(9183, "echo", "trailing=body");
    ws2->stop();
    ws2.reset();

    std::string captured_stderr = end_capture(stderr_cap, STDERR_FILENO);

    LT_CHECK_EQ(res1, 0);
    LT_CHECK_EQ(res2, 0);

    // The warning is fired through the project's tagged-prefix shape and
    // names the env var, the SECURITY WARNING marker, and the
    // credential / PII risk.
    LT_CHECK(captured_stderr.find("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY")
             != std::string::npos);
    LT_CHECK(captured_stderr.find("SECURITY WARNING")
             != std::string::npos);
    // Names the risk explicitly so an operator scanning logs sees what's at
    // stake.
    LT_CHECK(captured_stderr.find("credentials")
             != std::string::npos);

    // Idempotence: only one occurrence of the warning marker across two
    // start() calls.
    auto count_substring = [](const std::string& hay,
                              const std::string& needle) {
        size_t n = 0;
        size_t pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            ++n;
            pos += needle.size();
        }
        return n;
    };
    LT_CHECK_EQ(count_substring(captured_stderr, "SECURITY WARNING"),
                static_cast<size_t>(1));
LT_END_AUTO_TEST(startup_warning_emitted_exactly_once_across_multiple_webservers)

// Runs after startup_warning_emitted_exactly_once_across_multiple_webservers
// above (see the test-order note there) -- the one-shot startup warning has
// already latched by this point, so this test only exercises the per-request
// stdout body dump, which is not one-shot and fires on every dispatch.
LT_BEGIN_AUTO_TEST(dump_set_suite, dump_body_to_stdout_when_env_set)
    auto stdout_cap = begin_capture(STDOUT_FILENO, "set_stdout2");

    auto ws3 = std::make_unique<webserver>(
        create_webserver(9185)
            .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
            .max_threads(2));
    auto r3 = std::make_shared<echo_resource>();
    ws3->register_path("echo", r3);
    ws3->start(false);
    CURLcode res3 = run_one_post(9185, "echo", "solo=body-dump");
    ws3->stop();
    ws3.reset();

    std::string captured_stdout = end_capture(stdout_cap, STDOUT_FILENO);

    LT_CHECK_EQ(res3, 0);
    LT_CHECK(captured_stdout.find("Writing content:") != std::string::npos);
    LT_CHECK(captured_stdout.find("solo=body-dump") != std::string::npos);
LT_END_AUTO_TEST(dump_body_to_stdout_when_env_set)

LT_BEGIN_AUTO_TEST_ENV()
#if defined(_WIN32) && !defined(__CYGWIN__)
    // reason: this suite's positive assertions rely on the POSIX stdout/stderr
    // capture harness (stream_capture_helpers.hpp), which does not behave under
    // MSYS2/mingw — the dump itself works ("Writing content:" is emitted) but
    // the captured buffers come back empty, so the presence/count checks here
    // fail while the absence-based unset/zero variants pass vacuously. Skip the
    // whole binary on Windows (exit 77 -> Automake SKIP).
    std::fprintf(stderr, "[SKIP] debug_dump_request_body_set: POSIX stdout/"
                         "stderr capture harness unavailable on Windows\n");
    return 77;
#else
    // Opt in BEFORE the test bodies run (and BEFORE the magic-static
    // cache in the body pipeline is initialised on first dispatch).
    ::setenv("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY", "1", 1);
#endif
    // Kept outside the #if/#else so AUTORUN_TESTS() (which declares the
    // __lt_result__ that LT_END_AUTO_TEST_ENV returns) is always compiled;
    // on Windows it is unreachable after the return 77 above.
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
