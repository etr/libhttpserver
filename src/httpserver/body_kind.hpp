/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

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

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_BODY_KIND_HPP_
#define SRC_HTTPSERVER_BODY_KIND_HPP_

#include <cstdint>

namespace httpserver {

// Tag identifying which subclass of detail::response_body a given http_response is
// currently holding. Consumers reach this through http_response::kind()
// and should never have to name detail::response_body directly — the
// enum is the only consumer-visible part of the body hierarchy.
//
// `empty` is enumerator 0 so a value-initialised body_kind{} matches the
// "no body" state, which is what a default-constructed
// http_response reports.
//
// Underlying type is pinned to std::uint8_t so that future additions
// stay within a single byte and do not silently grow http_response. The
// fixed underlying type also makes the enum forward-declarable, although
// http_response.hpp will still pull in this full header (consumers will
// name the enumerators).
enum class body_kind : std::uint8_t {
    empty,
    string,    // NOLINT(build/include_what_you_use) - enumerator, not std::string
    file,
    iovec,
    pipe,
    deferred,
    // RFC-7616 Digest auth challenge body. The body is a body-only
    // MHD_Response (the "access denied" payload); the WWW-Authenticate header
    // with nonce/opaque/qop/algorithm parameters is attached by the dispatch
    // path via MHD_queue_auth_required_response3 rather than by the body's
    // own materialize() output. Carrying the kind lets the dispatch hot path
    // branch onto the auth-required queueing API without naming any backend
    // type from http_response.hpp.
    digest_challenge,
};

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_BODY_KIND_HPP_
