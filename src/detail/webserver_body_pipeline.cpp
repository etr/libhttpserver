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
#include "httpserver/detail/modded_request.hpp"

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

// Wrap the body_chunk firing site so requests_answer_second_step stays a
// flat sequence of small steps. Returns true iff a hook short-circuited
// and the caller should signal MHD that the chunk was consumed (returns
// MHD_YES with *upload_data_size = 0). Side effects on short-circuit:
//   - mr->response is populated with the hook-supplied response,
//   - mr->skip_handler is set so finalize_answer routes through the
//     skip branch,
//   - any in-flight MHD_PostProcessor is destroyed (32 KB buffer freed).
bool fire_and_maybe_short_circuit_body_chunk(webserver_impl* impl,
                                             modded_request* mr,
                                             const char* upload_data,
                                             size_t upload_data_size) {
    // ctx.offset is sourced from body_bytes_seen (not get_content().size())
    // so it accumulates correctly even when put_processed_data_to_content
    // is false and a post-processor is active (in that case grow_content
    // is skipped, so get_content().size() would stay at 0 for every chunk).
    //
    // ctx.is_final is hard-coded false: no production path fires a
    // final-chunk signal. End-of-body is signalled by MHD's zero-size
    // upload callback (the *upload_data_size == 0 call that routes to
    // complete_request), which never reaches this fire site.
    ::httpserver::body_chunk_ctx ctx{
        mr->request.get(),
        std::as_bytes(std::span<const char>(upload_data, upload_data_size)),
        mr->body_bytes_seen,
        /*is_final=*/false};
    mr->body_bytes_seen += upload_data_size;
    auto sc = impl->fire_body_chunk(ctx);
    if (!sc) return false;
    mr->response.emplace(std::move(*sc));
    mr->skip_handler = true;
    if (mr->pp != nullptr) {
        MHD_destroy_post_processor(mr->pp);
        mr->pp = nullptr;
    }
    return true;
}

// Feed @p upload_data through MHD's post processor (when one is
// attached) and close any open upload-target stream. Pulled out of the
// second_step orchestrator so the orchestrator stays under the CCN bar.
void run_post_processor_if_attached(modded_request* mr,
                                    webserver* parent,
                                    const char* upload_data,
                                    size_t upload_data_size) {
    if (mr->pp == nullptr) return;
    // Redundant-but-harmless refresh: answer_to_connection already
    // hoisted mr->ws = parent at request start (and complete_request
    // relies on that hoist). Kept as belt-and-suspenders; it never
    // changes the value.
    mr->ws = parent;
    MHD_post_process(mr->pp, upload_data, upload_data_size);
    if (mr->upload_ostrm != nullptr && mr->upload_ostrm->is_open()) {
        mr->upload_ostrm->close();
    }
}

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

MHD_Result webserver_impl::requests_answer_first_step(MHD_Connection* connection, struct detail::modded_request* mr) {
    // The http_request constructor calls pick_resource(connection) internally
    // to locate the per-connection arena installed by connection_notify via
    // MHD_CONNECTION_INFO_SOCKET_CONTEXT, then allocates the http_request_impl
    // from that arena. The arena plumbing is therefore implicit at this call
    // site but explicit within the constructor.
    mr->request.reset(new http_request(connection, parent->config.unescaper));
    mr->request->set_file_cleanup_callback(parent->config.file_cleanup_callback);
    // Propagate the redaction-bypass bit so operator<< honours
    // the builder opt-in for every request the webserver dispatches.
    mr->request->set_expose_credentials_in_logs(parent->config.expose_credentials_in_logs);

    // request_received hook. Fires after the http_request is
    // populated but before any body bytes are read (and before any
    // post-processor is created). Mutable ref so a hook may adjust
    // per-request state. Short-circuit: stash the response, mark
    // skip-to-finalize, and return MHD_YES. MHD will call back into
    // requests_answer_second_step with *upload_data_size == 0, which
    // routes through complete_request -> finalize_answer, where the
    // skip_handler branch goes straight to materialize_and_queue_response.
    // No post-processor exists at this point, so no teardown is needed.
    if (has_hooks_for(::httpserver::hook_phase::request_received)) {
        ::httpserver::request_received_ctx ctx{
            mr->request.get(),
            std::chrono::steady_clock::now()};
        if (auto sc = fire_request_received(ctx)) {
            mr->response.emplace(std::move(*sc));
            mr->skip_handler = true;
            return MHD_YES;
        }
    }

    if (!mr->has_body) {
        return MHD_YES;
    }

    mr->request->set_content_size_limit(parent->config.content_size_limit);
    const char *encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, http_utils::http_header_content_type);

    if (parent->config.post_process_enabled &&
        (nullptr != encoding &&
            ((0 == strncasecmp(http_utils::http_post_encoding_form_urlencoded, encoding, strlen(http_utils::http_post_encoding_form_urlencoded))) ||
             (0 == strncasecmp(http_utils::http_post_encoding_multipart_formdata, encoding, strlen(http_utils::http_post_encoding_multipart_formdata)))))) {
        const size_t post_memory_limit(32 * 1024);  // Same as #MHD_POOL_SIZE_DEFAULT
        mr->pp = MHD_create_post_processor(connection, post_memory_limit, &webserver_impl::post_iterator, mr);
    } else {
        mr->pp = nullptr;
    }
    return MHD_YES;
}

MHD_Result webserver_impl::requests_answer_second_step(MHD_Connection* connection, const char* method,
        const char* version, const char* upload_data,
        size_t* upload_data_size, struct detail::modded_request* mr) {
    if (0 == *upload_data_size) return complete_request(connection, mr, version, method);

    if (!mr->has_body) {
        *upload_data_size = 0;
        return MHD_YES;
    }

    // A prior pre-handler short-circuit (request_received in
    // first_step, or body_chunk on an earlier chunk) already populated
    // mr->response. Consume the chunk so MHD advances; the next
    // *upload_data_size == 0 callback will route to finalize_answer's
    // skip_handler branch.
    if (mr->skip_handler) {
        *upload_data_size = 0;
        return MHD_YES;
    }

    // body_chunk hook fires per chunk BEFORE the bytes are
    // appended to mr->request / fed to MHD_post_process.
    if (has_hooks_for(::httpserver::hook_phase::body_chunk)) {
        if (fire_and_maybe_short_circuit_body_chunk(
                this, mr, upload_data, *upload_data_size)) {
            *upload_data_size = 0;
            return MHD_YES;
        }
    }

    // Raw request-body dump, opt-in via the
    // env var LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY. Default behaviour
    // is silent on RELEASE *and* DEBUG builds -- the env var is the
    // only gate, so a debug build accidentally shipped to production
    // still does not leak credentials/PII unless the operator opted
    // in. See docs/debug-env-vars.md for the security warning and
    // the one-shot startup notice that fires when the env var is set.
    // The operator<< redaction policy does NOT cover this
    // code path: raw bytes are written verbatim.
    if (debug_dump_request_body_opted_in()) {
        std::cout << "Writing content: ";
        std::cout.write(upload_data,
                        static_cast<std::streamsize>(*upload_data_size));
        std::cout << std::endl;
    }
    // The post iterator is only created from the libmicrohttpd for content of type
    // multipart/form-data and application/x-www-form-urlencoded
    // all other content (which is indicated by mr-pp == nullptr)
    // has to be put to the content even if put_processed_data_to_content is set to false
    if (mr->pp == nullptr || parent->config.put_processed_data_to_content) {
        mr->request->grow_content(upload_data, *upload_data_size);
    }
    run_post_processor_if_attached(mr, parent, upload_data, *upload_data_size);

    *upload_data_size = 0;
    return MHD_YES;
}

}  // namespace detail
}  // namespace httpserver
