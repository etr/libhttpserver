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

// TASK-047 -- The two body-pipeline stages
// (requests_answer_first_step / requests_answer_second_step) extracted
// from webserver_request.cpp to keep that TU under the 500-LOC ceiling
// after adding the request_received and body_chunk firing sites
// (matches the TASK-046 split pattern that carved
// webserver_callbacks_lifecycle.cpp out of webserver_callbacks.cpp).
//
// Also hosts the small anon-ns helper that wraps the body_chunk firing
// site, both to keep the second-step orchestrator at CCN <= 10 and to
// make the short-circuit teardown path single-sourced.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <microhttpd.h>

#include <strings.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#ifdef DEBUG
#include <iostream>
#endif  // DEBUG
#include <optional>
#include <span>
#include <string>
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

// Wrap the body_chunk firing site so requests_answer_second_step stays a
// flat sequence of small steps. Returns true iff a hook short-circuited
// and the caller should signal MHD that the chunk was consumed (returns
// MHD_YES with *upload_data_size = 0). Side effects on short-circuit:
//   - mr->response_ is populated with the hook-supplied response,
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
    ::httpserver::body_chunk_ctx ctx{
        mr->dhr.get(),
        std::as_bytes(std::span<const char>(upload_data, upload_data_size)),
        mr->body_bytes_seen,
        /*is_final=*/false};
    mr->body_bytes_seen += upload_data_size;
    auto sc = impl->fire_body_chunk(ctx);
    if (!sc) return false;
    mr->response_.emplace(std::move(*sc));
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
    mr->ws = parent;
    MHD_post_process(mr->pp, upload_data, upload_data_size);
    if (mr->upload_ostrm != nullptr && mr->upload_ostrm->is_open()) {
        mr->upload_ostrm->close();
    }
}

}  // namespace

MHD_Result webserver_impl::requests_answer_first_step(MHD_Connection* connection, struct detail::modded_request* mr) {
    mr->dhr.reset(new http_request(connection, parent->unescaper));
    mr->dhr->set_file_cleanup_callback(parent->file_cleanup_callback);

    // TASK-047 -- request_received hook. Fires after the http_request is
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
            mr->dhr.get(),
            std::chrono::steady_clock::now()};
        if (auto sc = fire_request_received(ctx)) {
            mr->response_.emplace(std::move(*sc));
            mr->skip_handler = true;
            return MHD_YES;
        }
    }

    if (!mr->has_body) {
        return MHD_YES;
    }

    mr->dhr->set_content_size_limit(parent->content_size_limit);
    const char *encoding = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, http_utils::http_header_content_type);

    if (parent->post_process_enabled &&
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

    // TASK-047 -- a prior pre-handler short-circuit (request_received in
    // first_step, or body_chunk on an earlier chunk) already populated
    // mr->response_. Consume the chunk so MHD advances; the next
    // *upload_data_size == 0 callback will route to finalize_answer's
    // skip_handler branch.
    if (mr->skip_handler) {
        *upload_data_size = 0;
        return MHD_YES;
    }

    // TASK-047 -- body_chunk hook fires per chunk BEFORE the bytes are
    // appended to mr->dhr / fed to MHD_post_process.
    if (has_hooks_for(::httpserver::hook_phase::body_chunk)) {
        if (fire_and_maybe_short_circuit_body_chunk(
                this, mr, upload_data, *upload_data_size)) {
            *upload_data_size = 0;
            return MHD_YES;
        }
    }

#ifdef DEBUG
    std::cout << "Writing content: " << std::string(upload_data, *upload_data_size) << std::endl;
#endif  // DEBUG
    // The post iterator is only created from the libmicrohttpd for content of type
    // multipart/form-data and application/x-www-form-urlencoded
    // all other content (which is indicated by mr-pp == nullptr)
    // has to be put to the content even if put_processed_data_to_content is set to false
    if (mr->pp == nullptr || parent->put_processed_data_to_content) {
        mr->dhr->grow_content(upload_data, *upload_data_size);
    }
    run_post_processor_if_attached(mr, parent, upload_data, *upload_data_size);

    *upload_data_size = 0;
    return MHD_YES;
}

}  // namespace detail
}  // namespace httpserver
