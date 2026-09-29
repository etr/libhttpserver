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

#ifndef SRC_HTTPSERVER_HTTP_REQUEST_HEAD_HPP_
#define SRC_HTTPSERVER_HTTP_REQUEST_HEAD_HPP_

#include <string>

#include <httpserver/http/fields.hpp>
#include <httpserver/http/method.hpp>
#include <httpserver/http/protocol.hpp>

namespace httpserver {

namespace http {

// Seed view of a request head (PRD-V3N-REQ-019). Plain aggregate,
// default constructible, copyable and movable.
//
// Invariants:
// - raw_target holds the exact received bytes of the request-target
//   (including any percent-escapes, query, and fragment). It is never
//   rewritten by the library.
// - route_path is produced only by validation/normalization and is the
//   sole input to route matching. It is a distinct member, so a
//   normalized path and the received target can (and routinely do)
//   differ explicitly.
//
// head_fields carries the header fields; trailers get their own fields
// instance. Later milestones extend this struct with TLS/peer metadata.
struct request_head {
    std::string  raw_target;
    std::string  route_path;
    method       request_method;
    protocol     request_protocol = protocol::http_1_0;
    fields       head_fields;
};

}  // namespace http

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_REQUEST_HEAD_HPP_
