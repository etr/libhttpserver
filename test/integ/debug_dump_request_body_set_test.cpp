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

// TASK-074: when LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY is set, the
// server MUST:
//   - write a one-shot SECURITY WARNING to stderr on the first
//     webserver::start() call (and only the first, even if multiple
//     webservers start in the same process);
//   - write each POST body chunk to stdout prefixed by
//     "Writing content:".
//
// The env var is opted in via setenv() at the top of main() before
// any libhttpserver code runs. Both assertions live in a single
// binary (the magic-static body-pipeline cache means the first
// observation locks in for the rest of the process).

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

using httpserver::webserver;
using httpserver::create_webserver;
using httpserver::http_request;
using httpserver::http_response;
using httpserver::http_resource;

namespace {

class echo_resource : public http_resource {
 public:
    http_response render(const http_request&) override {
        return http_response::string("OK");
    }
};

struct stream_capture {
    int saved_fd = -1;
    int captured_fd = -1;
    std::string path;
};

stream_capture begin_capture(int fileno_to_capture, const char* tmp_label) {
    stream_capture c;
    char tpl[256];
    std::snprintf(tpl, sizeof(tpl),
                  "/tmp/libhttpserver_%s_XXXXXX", tmp_label);
    c.captured_fd = mkstemp(tpl);
    c.path = tpl;
    c.saved_fd = dup(fileno_to_capture);
    if (fileno_to_capture == STDOUT_FILENO) std::fflush(stdout);
    else if (fileno_to_capture == STDERR_FILENO) std::fflush(stderr);
    dup2(c.captured_fd, fileno_to_capture);
    return c;
}

std::string end_capture(stream_capture& c, int fileno_to_restore) {
    if (fileno_to_restore == STDOUT_FILENO) std::fflush(stdout);
    else if (fileno_to_restore == STDERR_FILENO) std::fflush(stderr);
    dup2(c.saved_fd, fileno_to_restore);
    ::close(c.saved_fd);
    ::close(c.captured_fd);
    FILE* f = std::fopen(c.path.c_str(), "rb");
    std::string out;
    if (f != nullptr) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
            out.append(buf, n);
        }
        std::fclose(f);
    }
    ::unlink(c.path.c_str());
    return out;
}

size_t writefunc(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* s = static_cast<std::string*>(userp);
    s->append(static_cast<const char*>(contents), size * nmemb);
    return size * nmemb;
}

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

LT_BEGIN_AUTO_TEST(dump_set_suite, dump_and_warn_then_warn_only_once)
    auto stdout_cap = begin_capture(STDOUT_FILENO, "set_stdout");
    auto stderr_cap = begin_capture(STDERR_FILENO, "set_stderr");

    // ---- First webserver: should warn + dump ----------------------------
    auto ws1 = std::make_unique<webserver>(
        create_webserver(9182)
            .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
            .max_threads(2));
    auto r1 = std::make_shared<echo_resource>();
    ws1->register_path("echo", r1);
    ws1->start(false);
    CURLcode res1 = run_one_post(9182, "echo", "secret=opted-in-body");
    ws1->stop();
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

    std::string captured_stdout = end_capture(stdout_cap, STDOUT_FILENO);
    std::string captured_stderr = end_capture(stderr_cap, STDERR_FILENO);

    LT_CHECK_EQ(res1, 0);
    LT_CHECK_EQ(res2, 0);

    // ---- stdout assertions --------------------------------------------
    LT_CHECK(captured_stdout.find("Writing content:") != std::string::npos);
    LT_CHECK(captured_stdout.find("secret=opted-in-body") != std::string::npos);
    LT_CHECK(captured_stdout.find("trailing=body") != std::string::npos);

    // ---- stderr assertions --------------------------------------------
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
LT_END_AUTO_TEST(dump_and_warn_then_warn_only_once)

LT_BEGIN_AUTO_TEST_ENV()
    // Opt in BEFORE the test bodies run (and BEFORE the magic-static
    // cache in the body pipeline is initialised on first dispatch).
    ::setenv("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY", "1", 1);
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
