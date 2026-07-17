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

// http_response_factories.hpp -- static named-constructor declarations of
// class http_response.
//
// This header carries static member DECLARATIONS only; it is meant to be
// included from WITHIN the body of `class http_response` defined in
// httpserver/http_response.hpp. Including it in any other context produces
// a compile error (see the inner gate below). The out-of-line definitions
// for every declaration here live in src/http_response_factories.cpp.
//
// Keeping this surface in its own file lets the http_response.hpp class
// definition stay under the project-wide per-file line-count ceiling
// (FILE_LOC_MAX in scripts/check-file-size.sh) without splitting the
// public class across translation units.
#ifndef SRC_HTTPSERVER_HTTP_RESPONSE_FACTORIES_HPP_
#define SRC_HTTPSERVER_HTTP_RESPONSE_FACTORIES_HPP_

#ifndef SRC_HTTPSERVER_HTTP_RESPONSE_HPP_INSIDE_CLASS_
#error "httpserver/http_response_factories.hpp must be included from inside the http_response class body in <httpserver/http_response.hpp>."
#endif

     // -----------------------------------------------------------------
     // Static factories.
     //
     // Each factory placement-news the corresponding detail::body
     // subclass into the response's SBO buffer (or, if the body ever
     // exceeds 64 bytes, onto the heap via ::operator new(sizeof(T))
     // so the destructor's matched ::operator delete pairs cleanly).
     // Replaces the v1 polymorphic *_response subclasses.
     //
     // Status-code defaults match v1: 200 for content-bearing bodies,
     // 204 for empty(), 401 for unauthorized().
     // -----------------------------------------------------------------

     // Construct a response carrying a string body. The Content-Type
     // header defaults to "text/plain"; pass a different value (for
     // example "application/json") to override. The body string is
     // stored by move so callers retain no aliasing.
     // Throws std::invalid_argument if content_type contains CR, LF, or NUL.
     [[nodiscard]] static http_response string(
         std::string body,
         std::string content_type = "text/plain");

     // Construct a response that streams a file from disk. Does NOT
     // throw on a missing or unreadable path — failure is observable at
     // dispatch time (the materialized MHD_Response is null and the
     // dispatch path renders a 500). Mirrors v1 file_response semantics.
     [[nodiscard]] static http_response file(std::string path);

     // Construct a response from a span of scatter/gather buffers. The
     // entries array is deep-copied into the body so the span need not
     // outlive the response, but the buffers each entry's `base` points
     // at remain BORROWED — they must outlive the response (and the
     // MHD_Response that response materializes).
     [[nodiscard]] static http_response iovec(
         std::span<const iovec_entry> entries);

     // Construct a response that streams from a pipe read-end. The
     // factory takes ownership of `fd` immediately. The fd is closed
     // when the materialized MHD_Response is destroyed; if the response
     // is never materialized, the http_response's destructor closes
     // it. Callers MUST NOT close `fd` after handing it off.
     [[nodiscard]] static http_response pipe(int fd);

     // Construct an empty (no-payload) response. Defaults to 204
     // No Content, matching v1 empty_response. The optional `mhd_flags`
     // argument forwards to MHD_set_response_options on the materialized
     // MHD_Response — pass `MHD_RF_HEAD_ONLY_RESPONSE` to send a HEAD-only
     // response with headers but no body, etc.
     [[nodiscard]] static http_response empty(int mhd_flags = 0);

     // Construct a response that streams from a producer callback.
     // libmicrohttpd invokes `producer(pos, buf, max)` whenever it
     // needs more bytes; the producer should return the number of
     // bytes written, MHD_CONTENT_READER_END_OF_STREAM, or
     // MHD_CONTENT_READER_END_WITH_ERROR. The producer is stored by
     // move; large captures may force std::function to heap-allocate
     // internally (independent of http_response's own SBO).
     [[nodiscard]] static http_response deferred(
         std::function<ssize_t(std::uint64_t, char*, std::size_t)> producer);

     /// Construct a 401 Unauthorized response with a WWW-Authenticate
     /// header of the form `<scheme> realm="<realm>"`. Replaces v1's
     /// basic_auth_fail_response and digest_auth_fail_response.
     ///
     /// Use this overload for `"Basic"` and for RFC-2069-compatible
     /// static `Digest realm="..."` challenges.  For full RFC 7616 §3.3
     /// Digest challenges with `nonce`, `opaque`, `algorithm`, and `qop`
     /// parameters — required by strict RFC-7616 parsers — use the
     /// `unauthorized(digest_challenge)` overload below.  The dispatch
     /// path detects a `body_kind::digest_challenge` body and routes
     /// through libmicrohttpd's `MHD_queue_auth_required_response3`,
     /// driving the per-connection nonce state machine.  See RFC 7616
     /// §3.3 (challenge format), §3.4 (Authorization validation), and
     /// §5.10 (security considerations).
     ///
     /// @see digest_challenge
     [[nodiscard]] static http_response unauthorized(
         std::string_view scheme,
         std::string_view realm,
         std::string body = {});  // NOLINT(build/include_what_you_use)

     /// Construct an RFC 7616 §3.3 Digest 401 Unauthorized response.
     ///
     /// The returned response carries a `body_kind::digest_challenge`
     /// body that the webserver dispatch path recognises: instead of
     /// `MHD_queue_response`, the dispatch site invokes
     /// `MHD_queue_auth_required_response3` (libmicrohttpd >= 0x00097701)
     /// with the parameters carried on the body.  libmicrohttpd then
     /// writes the authoritative `WWW-Authenticate: Digest ...`
     /// challenge with `nonce`, `opaque`, `algorithm`, `qop`, and
     /// (optionally) `charset=UTF-8` / `userhash=true`.
     ///
     /// The nonce is generated and replay-tracked by libmicrohttpd
     /// itself, HMAC-keyed by `create_webserver().digest_auth_random`
     /// and replay-windowed by `create_webserver().nonce_nc_size`.
     /// libhttpserver does not implement its own nonce store, in line
     /// with the "MHD's MD5/SHA-256 helpers remain the underlying
     /// primitive" architectural constraint.
     ///
     /// The `opaque` field is optional — when left empty, the dispatch
     /// path substitutes a per-webserver-instance random hex string
     /// generated once at startup.
     ///
     /// Header-injection guards: `realm`, `opaque`, `domain`, and `body`
     /// are rejected with `std::invalid_argument` if they contain CR,
     /// LF, or NUL (CWE-113).
     ///
     /// Always declared, regardless of `HAVE_DAUTH` (public
     /// declarations must not be conditionally compiled). On a
     /// `HAVE_DAUTH`-off build the call throws
     /// `feature_unavailable("digest_auth", "HAVE_DAUTH")`
     /// instead of constructing a response.
     ///
     /// @see http_response::unauthorized(std::string_view,std::string_view,std::string)
     [[nodiscard]] static http_response unauthorized(
         digest_challenge challenge);

#endif  // SRC_HTTPSERVER_HTTP_RESPONSE_FACTORIES_HPP_
