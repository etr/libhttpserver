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

// TASK-074: when LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY is unset (or
// "0"), the server MUST NOT write request bodies to stdout, and MUST
// NOT emit the security warning to stderr -- on ANY build
// configuration. This test binary unsets the env var at the top of
// main() (before any libhttpserver code runs) and asserts both
// streams stay clean across a POST.

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

// Redirect FILENO to a tmp file via dup2 so we can read what the
// process writes to stdout/stderr in-test (the magic-static cache in
// the body pipeline means we cannot easily re-test a different value
// of the env var after the first webserver dispatch in the same
// process).
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

}  // namespace

LT_BEGIN_SUITE(dump_unset_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(dump_unset_suite)

LT_BEGIN_AUTO_TEST(dump_unset_suite, no_stdout_no_warning)
    auto stdout_cap = begin_capture(STDOUT_FILENO, "unset_stdout");
    auto stderr_cap = begin_capture(STDERR_FILENO, "unset_stderr");

    auto ws = std::make_unique<webserver>(
        create_webserver(9181)
            .start_method(httpserver::http::http_utils::INTERNAL_SELECT)
            .max_threads(2));
    auto resource = std::make_shared<echo_resource>();
    ws->register_path("echo", resource);
    ws->start(false);

    curl_global_init(CURL_GLOBAL_ALL);
    std::string body;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:9181/echo");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "secret=hello-world");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    ws->stop();

    std::string captured_stdout = end_capture(stdout_cap, STDOUT_FILENO);
    std::string captured_stderr = end_capture(stderr_cap, STDERR_FILENO);

    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(body, std::string("OK"));

    // Core acceptance criterion: env-var unset => silent on stdout
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
    // Scrub env var BEFORE the test bodies run, defensive against an
    // inherited LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY in the parent
    // shell.
    ::unsetenv("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY");
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
