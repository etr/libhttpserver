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

// clf_access_log.cpp -- Common Log Format access logger written as a
// response_sent lifecycle hook, demonstrating the resolution of issues
// #281 and #69. libhttpserver v1's log_access(fn) callback handed the
// user a single string and was invoked at request-arrival time, so the
// status code, byte count, and request duration were not yet known.
// Issues #281 and #69 asked for a logger that could emit a real CLF /
// `time-taken` line.
//
// In v2.0 the response_sent hook (DR-012 §4.10) fires immediately after
// MHD_queue_response and carries the structured context users have been
// asking for:
//   - ctx.status        -- HTTP status code
//   - ctx.bytes_queued  -- logical body size (Content-Length, when known)
//   - ctx.elapsed       -- steady_clock nanoseconds from
//                          answer_to_connection's first invocation
//
// This example uses those fields to emit a Common Log Format line on
// stdout. The library no longer hard-codes a logging format -- you do.
//
// Run:
//   ./clf_access_log     # blocks; serves on :8080
//   curl http://localhost:8080/hello
//   curl http://localhost:8080/hello?name=world
//
// Expected output (per request):
//   - - - [26/May/2026:14:01:23 +0000] "GET /hello HTTP/1.1" 200 13 1

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <httpserver.hpp>

namespace hs = httpserver;

namespace {

class hello_resource : public hs::http_resource {
 public:
    hs::http_response render_get(const hs::http_request&) override {
        return hs::http_response::string("Hello, World!");
    }
};

// SECURITY (CWE-117): sanitize a string_view for CLF output.
// Replace ASCII control characters (< 0x20 or == 0x7F) with '-' to
// prevent a client from injecting additional log lines by embedding
// newlines or carriage-returns in the request path or method.
std::string sanitize_clf(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv) {
        out += (c < 0x20 || c == 0x7f) ? '-' : static_cast<char>(c);
    }
    return out;
}

void emit_clf_line(const hs::response_sent_ctx& ctx) {
    // ctx.request is guaranteed non-null at the response_sent fire site
    // (documented in response_sent_ctx). Guard here for defence-in-depth.
    if (ctx.request == nullptr) return;

    std::time_t now = std::time(nullptr);
    char ts[32];
    std::tm tm_buf;
#if defined(_WIN32) && !defined(__CYGWIN__)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    std::strftime(ts, sizeof(ts), "%d/%b/%Y:%H:%M:%S %z", &tm_buf);

    int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                     ctx.elapsed).count();
    // Sanitize before embedding in the CLF line to prevent log injection.
    // The protocol version is the one the client actually advertised,
    // read from ctx.request->get_version() (TASK-018). It is sanitized
    // like method and path for defence-in-depth even though it comes from
    // MHD's request-line parse.
    const std::string method  = sanitize_clf(ctx.request->get_method());
    const std::string path    = sanitize_clf(ctx.request->get_path());
    const std::string version = sanitize_clf(ctx.request->get_version());
    std::printf("- - - [%s] \"%s %s %s\" %d %zu %lld\n",
                ts,
                method.c_str(),
                path.c_str(),
                version.c_str(),
                ctx.status,
                ctx.bytes_queued,
                static_cast<long long>(ms));  // NOLINT(runtime/int)
    std::fflush(stdout);
}

}  // namespace

int main() {
    hs::webserver ws{hs::create_webserver(8080)};

    auto h = ws.add_hook(hs::hook_phase::response_sent,
        std::function<void(const hs::response_sent_ctx&)>(emit_clf_line));

    auto resource = std::make_shared<hello_resource>();
    ws.register_path("/hello", resource);

    ws.start(true);
    return 0;
}
