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

// request_pipeline -- behavior service (DR-014, §4.11): the libmicrohttpd
// re-entrant body-accumulation state machine. Builds the http_request and
// fires request_received (first step), reads/short-circuits body chunks and
// runs the post-processor (second step), and hands the completed request to
// the dispatcher (complete_request -> request_dispatcher::finalize_answer).
//
// The webserver_impl::answer_to_connection static MHD trampoline does the
// per-request setup (start_time, standardized_url, method callback) and then
// forwards into first_step / second_step here.
//
// Holds const webserver_config& (body-read config), hook_dispatcher& (the
// request_received / body_chunk gates), and request_dispatcher& (finalize).
// A friend of http_request (constructs it + writes its per-request fields).
//
// Internal header; only reachable when compiling libhttpserver.
#if !defined(HTTPSERVER_COMPILATION)
#error "request_pipeline.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_REQUEST_PIPELINE_HPP_
#define SRC_HTTPSERVER_DETAIL_REQUEST_PIPELINE_HPP_

#include <microhttpd.h>

#include <cstddef>

#if MHD_VERSION < 0x00097002
typedef int MHD_Result;
#endif

namespace httpserver {

struct webserver_config;

namespace detail {

struct connection_context;
class hook_dispatcher;
class request_dispatcher;

class request_pipeline {
 public:
    request_pipeline(hook_dispatcher& hooks, request_dispatcher& dispatcher,
                     const webserver_config& config) noexcept
        : hooks_(hooks), dispatcher_(dispatcher), config_(config) {}

    request_pipeline(const request_pipeline&) = delete;
    request_pipeline& operator=(const request_pipeline&) = delete;
    request_pipeline(request_pipeline&&) = delete;
    request_pipeline& operator=(request_pipeline&&) = delete;
    ~request_pipeline() = default;

    // First MHD callback for a fresh request: construct the http_request,
    // fire request_received (short-circuits to skip_handler), and create the
    // post-processor for form/multipart bodies.
    MHD_Result requests_answer_first_step(MHD_Connection* connection,
                                          connection_context* conn);

    // Subsequent MHD callbacks: on a zero-size chunk hand off to
    // complete_request; otherwise fire body_chunk (short-circuit), optionally
    // dump the raw body (debug env var), grow the content, and run the
    // post-processor.
    MHD_Result requests_answer_second_step(MHD_Connection* connection,
                                           const char* method,
                                           const char* version,
                                           const char* upload_data,
                                           size_t* upload_data_size,
                                           connection_context* conn);

 private:
    // Stamp the request path/method/version and hand off to the dispatcher's
    // finalize_answer. Called by second_step on the end-of-body signal.
    MHD_Result complete_request(MHD_Connection* connection, connection_context* conn,
                                const char* version, const char* method);

    hook_dispatcher& hooks_;
    request_dispatcher& dispatcher_;
    const webserver_config& config_;
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_REQUEST_PIPELINE_HPP_
