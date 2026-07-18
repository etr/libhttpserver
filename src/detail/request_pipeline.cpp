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

// request_pipeline behavior service (DR-014 §4.11). The two body-pipeline
// stages + complete_request moved verbatim out of
// detail/webserver_body_pipeline.cpp / webserver_request.cpp (which keep the
// debug-dump free functions and the answer_to_connection trampoline /
// resolve_method_callback / should_skip_auth respectively). Rewiring:
// parent->config.* becomes config_.*, hook gates go through hooks_
// (hook_dispatcher), and complete_request hands off to dispatcher_
// (request_dispatcher). The redundant belt-and-suspenders `mr->ws = parent`
// refresh in the post-processor helper is dropped (answer_to_connection
// already set it and the comment noted it never changes the value).

#include "httpserver/detail/request_pipeline.hpp"

#include <microhttpd.h>
#include <strings.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "httpserver/create_webserver.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/detail/hook_dispatcher.hpp"
#include "httpserver/detail/modded_request.hpp"
#include "httpserver/detail/request_dispatcher.hpp"
#include "httpserver/detail/webserver_impl.hpp"

namespace httpserver {

using httpserver::http::http_utils;

namespace detail {

namespace {

// Wrap the body_chunk firing site so requests_answer_second_step stays a
// flat sequence of small steps. Returns true iff a hook short-circuited and
// the caller should signal MHD the chunk was consumed. Side effects on
// short-circuit: mr->response populated, mr->skip_handler set, any in-flight
// post-processor destroyed.
bool fire_and_maybe_short_circuit_body_chunk(hook_dispatcher& hooks,
                                             modded_request* mr,
                                             const char* upload_data,
                                             size_t upload_data_size) {
    // ctx.offset is sourced from body_bytes_seen (not get_content().size())
    // so it accumulates correctly even when put_processed_data_to_content is
    // false and a post-processor is active. ctx.is_final is hard-coded false:
    // end-of-body is signalled by MHD's zero-size upload callback, which
    // routes to complete_request and never reaches this fire site.
    ::httpserver::body_chunk_ctx ctx{
        mr->request.get(),
        std::as_bytes(std::span<const char>(upload_data, upload_data_size)),
        mr->body_bytes_seen,
        /*is_final=*/false};
    mr->body_bytes_seen += upload_data_size;
    auto sc = hooks.fire_body_chunk(ctx);
    if (!sc) return false;
    mr->response.emplace(std::move(*sc));
    mr->skip_handler = true;
    if (mr->pp != nullptr) {
        MHD_destroy_post_processor(mr->pp);
        mr->pp = nullptr;
    }
    return true;
}

// Feed @p upload_data through MHD's post processor (when one is attached)
// and close any open upload-target stream.
void run_post_processor_if_attached(modded_request* mr, const char* upload_data,
                                    size_t upload_data_size) {
    if (mr->pp == nullptr) return;
    MHD_post_process(mr->pp, upload_data, upload_data_size);
    if (mr->upload_ostrm != nullptr && mr->upload_ostrm->is_open()) {
        mr->upload_ostrm->close();
    }
}

}  // namespace

MHD_Result request_pipeline::requests_answer_first_step(
        MHD_Connection* connection, struct detail::modded_request* mr) {
    // The http_request constructor calls pick_resource(connection) internally
    // to locate the per-connection arena installed by connection_notify, then
    // allocates the http_request_impl from that arena.
    mr->request.reset(new http_request(connection, config_.unescaper));
    mr->request->set_file_cleanup_callback(config_.file_cleanup_callback);
    // Propagate the redaction-bypass bit so operator<< honours the builder
    // opt-in for every request the webserver dispatches.
    mr->request->set_expose_credentials_in_logs(
        config_.expose_credentials_in_logs);

    // request_received hook. Fires after the http_request is populated but
    // before any body bytes are read (and before any post-processor is
    // created). Short-circuit: stash the response, mark skip-to-finalize, and
    // return MHD_YES; MHD calls back into second_step with
    // *upload_data_size == 0, which routes through complete_request ->
    // finalize_answer's skip_handler branch.
    if (hooks_.has_hooks_for(::httpserver::hook_phase::request_received)) {
        ::httpserver::request_received_ctx ctx{
            mr->request.get(),
            std::chrono::steady_clock::now()};
        if (auto sc = hooks_.fire_request_received(ctx)) {
            mr->response.emplace(std::move(*sc));
            mr->skip_handler = true;
            return MHD_YES;
        }
    }

    if (!mr->has_body) {
        return MHD_YES;
    }

    mr->request->set_content_size_limit(config_.content_size_limit);
    const char *encoding = MHD_lookup_connection_value(connection,
        MHD_HEADER_KIND, http_utils::http_header_content_type);

    if (config_.post_process_enabled &&
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

MHD_Result request_pipeline::requests_answer_second_step(
        MHD_Connection* connection, const char* method, const char* version,
        const char* upload_data, size_t* upload_data_size,
        struct detail::modded_request* mr) {
    if (0 == *upload_data_size) return complete_request(connection, mr, version, method);

    if (!mr->has_body) {
        *upload_data_size = 0;
        return MHD_YES;
    }

    // A prior pre-handler short-circuit (request_received in first_step, or
    // body_chunk on an earlier chunk) already populated mr->response. Consume
    // the chunk so MHD advances; the next *upload_data_size == 0 callback
    // routes to finalize_answer's skip_handler branch.
    if (mr->skip_handler) {
        *upload_data_size = 0;
        return MHD_YES;
    }

    // body_chunk hook fires per chunk BEFORE the bytes are appended to
    // mr->request / fed to MHD_post_process.
    if (hooks_.has_hooks_for(::httpserver::hook_phase::body_chunk)) {
        if (fire_and_maybe_short_circuit_body_chunk(
                hooks_, mr, upload_data, *upload_data_size)) {
            *upload_data_size = 0;
            return MHD_YES;
        }
    }

    // Raw request-body dump, opt-in via LIBHTTPSERVER_DEBUG_DUMP_REQUEST_BODY.
    // Default behaviour is silent on RELEASE *and* DEBUG builds. The
    // operator<< redaction policy does NOT cover this path: raw bytes are
    // written verbatim. See docs/debug-env-vars.md.
    if (debug_dump_request_body_opted_in()) {
        std::cout << "Writing content: ";
        std::cout.write(upload_data,
                        static_cast<std::streamsize>(*upload_data_size));
        std::cout << std::endl;
    }
    // The post iterator is only created for multipart/form-data and
    // application/x-www-form-urlencoded; all other content (mr->pp == nullptr)
    // must be put to the content even if put_processed_data_to_content is false.
    if (mr->pp == nullptr || config_.put_processed_data_to_content) {
        mr->request->grow_content(upload_data, *upload_data_size);
    }
    run_post_processor_if_attached(mr, upload_data, *upload_data_size);

    *upload_data_size = 0;
    return MHD_YES;
}

MHD_Result request_pipeline::complete_request(MHD_Connection* connection,
        struct detail::modded_request* mr, const char* version,
        const char* method) {
    // mr->ws is pre-populated in answer_to_connection (hoisted there for
    // early-path request_completed coverage); no need to set it again here.
    mr->request->set_path(mr->standardized_url);
    mr->request->set_method(method);
    mr->request->set_version(version);

    return dispatcher_.finalize_answer(connection, mr);
}

}  // namespace detail
}  // namespace httpserver
