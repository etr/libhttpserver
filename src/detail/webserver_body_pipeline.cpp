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

// The two body-pipeline stages
// (requests_answer_first_step / requests_answer_second_step) extracted
// from webserver_request.cpp to keep that TU under the 500-LOC ceiling
// (the same split pattern that carved
// webserver_callbacks_lifecycle.cpp out of webserver_callbacks.cpp).
//
// Also hosts the small anon-ns helper that wraps the body_chunk firing
// site, both to keep the second-step orchestrator at CCN <= 10 and to
// make the short-circuit teardown path single-sourced.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <microhttpd.h>

#include <strings.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "httpserver/create_webserver.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/connection_context.hpp"

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

namespace {

// Process-wide print-once flag for the SECURITY WARNING
// emitted at the first webserver::start() that observes the env var.
// std::atomic<bool> + compare_exchange_strong matches the lock-free
// atomic style used elsewhere (e.g. g_arena_fallback_count). The
// idempotence is process-wide -- multiple webservers in the same
// process trigger exactly one stderr warning. Its only consumer,
// detail::maybe_warn_debug_dump_request_body(), is defined later in
// this same translation unit, which is what makes this anonymous-
// namespace (TU-local) visibility sufficient.
std::atomic<bool> g_debug_dump_warning_emitted{false};

}  // namespace

// Free function in namespace detail, declared in
// webserver_impl.hpp -- see the Doxygen comment there for the full
// contract (env var name, accepted-value rule, default-silent
// guarantee). The check is cached in a function-local static so
// getenv() is called at most once per process; subsequent setenv()
// calls are intentionally ignored (debug-knob semantics). C++11
// guarantees thread-safe init of function-local statics, so the first
// call from MHD's request-handling threads is safe even under
// concurrent first-touch.
bool debug_dump_request_body_opted_in() {
    static const bool enabled = [] {
        const char* v = std::getenv("LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY");
        return v != nullptr && v[0] != '\0' && std::string_view(v) != "0";
    }();
    return enabled;
}

// Free function in namespace detail, declared in
// webserver_impl.hpp. Called from webserver::start() before
// MHD_start_daemon. Emits a one-shot SECURITY WARNING to stderr when
// the env-var opt-in is observed, and forwards the same line to the
// owning webserver's log_error callback when one is wired. The flag
// is process-wide so multiple webservers do not flood stderr.
//
// Stderr is used (not std::cerr) to match the pre-existing
// fprintf-to-stderr pattern in src/http_request.cpp / src/webserver.cpp
// and to remain visible even when the user has not wired log_error.
//
// Both the stderr path and the log_error forwarding swallow any
// exception so a misconfigured logger cannot abort the daemon
// start sequence.
void maybe_warn_debug_dump_request_body(const webserver* parent) {
    if (!debug_dump_request_body_opted_in()) return;
    bool expected = false;
    if (!g_debug_dump_warning_emitted.compare_exchange_strong(
            expected, true)) {
        return;  // already warned this process
    }
    constexpr const char* msg =
        "[libhttpserver] SECURITY WARNING: "
        "LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY is set. "
        "Raw request bodies will be written to stdout, including "
        "credentials, session cookies, and PII. Unset the variable "
        "for production deployments. See docs/debug-env-vars.md.";
    std::fprintf(stderr, "%s\n", msg);
    std::fflush(stderr);
    if (parent != nullptr) {
        if (auto sink = parent->get_error_logger()) {
            try {
                sink(msg);
            } catch (...) {
                // swallow: misconfigured logger must not abort start()
            }
        }
    }
}

// requests_answer_first_step / requests_answer_second_step (and their
// body_chunk-fire and post-processor anon helpers) moved to the
// request_pipeline behavior service (detail/request_pipeline.cpp,
// DR-014 §4.11). This TU retains only the process-wide debug-dump opt-in
// helpers above, which are shared between request_pipeline (second_step) and
// webserver::start (the one-shot security warning).

}  // namespace detail
}  // namespace httpserver
