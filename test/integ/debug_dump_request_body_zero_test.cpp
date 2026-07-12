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

// TASK-074: when LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY is explicitly set
// to "0", the server MUST behave exactly as when the var is unset -- no
// request bodies written to stdout, and no security warning on stderr.
// This is a distinct code path from "unset" (see
// debug_dump_request_body_unset_test.cpp): the env var IS present in the
// environment, just with a falsy value, so this pins that the opt-in
// check treats "0" as off rather than merely checking for presence.
// This test binary sets the env var to "0" at the top of main() (before
// any libhttpserver code runs) and asserts both streams stay clean
// across a POST.

#include <curl/curl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "./httpserver.hpp"
#include "./littletest.hpp"
#include "./curl_helpers.hpp"

// Stream capture (dup2 to a tmp file) lets us read what the process
// writes to stdout/stderr in-test (the magic-static cache in the body
// pipeline means we cannot easily re-test a different value of the env
// var after the first webserver dispatch in the same process).
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

}  // namespace

LT_BEGIN_SUITE(dump_zero_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(dump_zero_suite)

LT_BEGIN_AUTO_TEST(dump_zero_suite, no_stdout_no_warning)
    auto stdout_cap = begin_capture(STDOUT_FILENO, "zero_stdout");
    auto stderr_cap = begin_capture(STDERR_FILENO, "zero_stderr");

    auto ws = std::make_unique<webserver>(
        create_webserver(9184)
            .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
            .max_threads(2));
    auto resource = std::make_shared<echo_resource>();
    ws->register_path("echo", resource);
    ws->start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string body;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:9184/echo");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "secret=hello-world");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    ws->stop();

    std::string captured_stdout = end_capture(stdout_cap, STDOUT_FILENO);
    std::string captured_stderr = end_capture(stderr_cap, STDERR_FILENO);

    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(body, std::string("OK"));

    // Core acceptance criterion: env-var set to "0" => silent on stdout
    // even though the body was POSTed.
    LT_CHECK(captured_stdout.find("Writing content:") == std::string::npos);
    LT_CHECK(captured_stdout.find("secret=hello-world") == std::string::npos);

    // No startup warning should reach stderr.
    LT_CHECK(captured_stderr.find("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY")
             == std::string::npos);
    LT_CHECK(captured_stderr.find("SECURITY WARNING")
             == std::string::npos);
LT_END_AUTO_TEST(no_stdout_no_warning)

LT_BEGIN_AUTO_TEST_ENV()
    // Opt out via "0" BEFORE the test bodies run, before the magic-static
    // cache in the body pipeline is initialised on first dispatch.
    ::setenv("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY", "0", 1);
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
