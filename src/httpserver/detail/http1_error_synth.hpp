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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

// Bare error response serialization of the HTTP/1 engine (TASK-108).
// The engine answers WITHOUT routing when a head never parsed or a
// framing decision was rejected: 400 for malformed syntax, 431 for a
// head-budget violation, 501 for an unsupported transfer coding, 503
// for an overload refusal. The wire form is deliberately bare -- the
// version-mirrored status line, "Connection: close" (the engine always
// closes after a synthesized error), "Content-Length: 0", no body --
// and it goes through the same response framer every real response
// uses, so version-conditional emission stays in exactly one place.
// error_pages.hpp is v2-coupled; this header is the self-contained v3
// synthesis. Header-only: the engine composes the bytes directly.

#if !defined(HTTPSERVER_COMPILATION)
#error "httpserver/detail/http1_error_synth.hpp is internal; only include it when compiling libhttpserver (HTTPSERVER_COMPILATION must be defined)."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_HTTP1_ERROR_SYNTH_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP1_ERROR_SYNTH_HPP_

#include <cstdint>
#include <string>

#include <httpserver/detail/http1_response_framer.hpp>
#include <httpserver/http/fields.hpp>
#include <httpserver/http/protocol.hpp>
#include <httpserver/http/request_head.hpp>
#include <httpserver/http/status.hpp>

namespace httpserver {

namespace detail {

class http1_error_synth {
 public:
    // Serializes the bare @p code answer for a @p version request.
    // @p clock rides the framer's Date seam (an empty clock omits the
    // Date field, which keeps the wire deterministic when the caller
    // does not want a timestamp).
    static std::string serialize(
        http::protocol version, std::uint16_t code,
        const http1_response_framer::clock_source& clock = {});
};

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_HTTP1_ERROR_SYNTH_HPP_
