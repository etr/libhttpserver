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

#ifndef SRC_HTTPSERVER_HTTP_RESPONSE_HPP_
#define SRC_HTTPSERVER_HTTP_RESPONSE_HPP_

#include <sys/types.h>          // ssize_t — for the deferred() producer

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include "httpserver/body_kind.hpp"
#include "httpserver/http_arg_value.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/iovec_entry.hpp"

struct MHD_Connection;
struct MHD_Response;

namespace httpserver {

// Forward-declared so http_response carries a `detail::body*` without
// pulling the private body hierarchy (and its <microhttpd.h> dependency)
// into every consumer translation unit. The complete type is required at
// destructor / move-op definition sites only; those live in the .cpp.
namespace detail { class body; }

/**
 * Class representing an abstraction for an Http Response. It is used from classes using these apis to send information through http protocol.
**/
// PRD-HDR-REQ-004 exemption (DR-003a): http_response is the v2 sealed
// value type and does NOT use the PIMPL pattern. It carries a 64-byte
// SBO buffer (`body_storage_`) so the polymorphic body lives inline for
// the common cases (string/empty/file/iovec/pipe/deferred), and falls
// back to a heap pointer for outsized bodies. Move-only (DR-005);
// copying a response would have to deep-copy the body, which is
// semantically wrong for fd-owning bodies and unnecessary in practice.
class http_response {
 public:
     // Public type-trait shim used by the SBO unit test (TASK-009) to
     // assert the exemption from PRD-HDR-REQ-004 without poking private
     // members. The trait check is `!std::is_same_v<body_pointer_type,
     // std::unique_ptr<detail::body>>`.
     using body_pointer_type = detail::body*;

     // SBO buffer size in bytes. Must equal the alignas/array spec on
     // body_storage_ below; the static_assert on alignof(http_response)
     // in http_response.cpp catches any drift.
     static constexpr std::size_t body_buf_size = 64;

     http_response() = default;

     explicit http_response(int response_code, const std::string& content_type):
         status_code_(response_code) {
             headers_[http::http_utils::http_header_content_type] = content_type;
     }

     // Move-only (DR-005, PRD-RSP-REQ-007). Copy ops are deleted because
     // a response's body may own non-copyable resources (file fds, pipe
     // fds, std::function targets) and a deep-copy would either silently
     // duplicate ownership or be a slice. v2 propagation is always by
     // move or by shared_ptr.
     http_response(const http_response&) = delete;
     http_response& operator=(const http_response&) = delete;

     // Out-of-line because both ops touch the complete type of
     // detail::body (placement-move via move_into(), destructor, or
     // ::operator delete).
     http_response(http_response&& other) noexcept;
     http_response& operator=(http_response&& other) noexcept;

     // Destructor stays virtual for the v1 subclass hierarchy (TASK-013
     // removes them; `final` lands then). Out-of-line because it calls
     // body_->~body() and ::operator delete(body_), both of which need
     // the complete type.
     virtual ~http_response();

     // Body-kind discriminator (TASK-010 AC). Mirrors the kind reported
     // by the underlying detail::body, but answered without a virtual
     // call: the kind is recorded into kind_ at factory time and
     // preserved across moves. TASK-011's dispatch path will consume
     // this for its kind-specific fast paths.
     [[nodiscard]] body_kind kind() const noexcept { return kind_; }

     // -----------------------------------------------------------------
     // Static factories (TASK-010, DR-005).
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
     // `size_hint` is reserved for forward compatibility — currently
     // ignored, may be used to advise libmicrohttpd of payload size in
     // a future revision.
     [[nodiscard]] static http_response pipe(int fd,
                                             std::size_t size_hint = 0);

     // Construct an empty (no-payload) response. Defaults to 204
     // No Content, matching v1 empty_response.
     [[nodiscard]] static http_response empty();

     // Construct a response that streams from a producer callback.
     // libmicrohttpd invokes `producer(pos, buf, max)` whenever it
     // needs more bytes; the producer should return the number of
     // bytes written, MHD_CONTENT_READER_END_OF_STREAM, or
     // MHD_CONTENT_READER_END_WITH_ERROR. The producer is stored by
     // move; large captures may force std::function to heap-allocate
     // internally (independent of http_response's own SBO).
     [[nodiscard]] static http_response deferred(
         std::function<ssize_t(std::uint64_t, char*, std::size_t)> producer);

     // Construct a 401 Unauthorized response with a WWW-Authenticate
     // header of the form `<scheme> realm="<realm>"`. Replaces v1's
     // basic_auth_fail_response and digest_auth_fail_response.
     //
     // Note: for "Digest" the response carries a static
     // WWW-Authenticate challenge but does NOT participate in
     // libmicrohttpd's nonce/opaque digest-auth state machine — that
     // was v1's MHD_queue_auth_required_response3-driven path which
     // requires connection-time state. Callers needing full digest
     // auth should reach for the dedicated MHD APIs directly.
     [[nodiscard]] static http_response unauthorized(
         std::string_view scheme,
         std::string_view realm,
         std::string body = {});

     /**
      * Method used to get a specified header defined for the response
      * @param key The header identification
      * @return a string representing the value assumed by the header
     **/
     const std::string& get_header(const std::string& key) {
         return headers_[key];
     }

     /**
      * Method used to get a specified footer defined for the response
      * @param key The footer identification
      * @return a string representing the value assumed by the footer
     **/
     const std::string& get_footer(const std::string& key) {
         return footers_[key];
     }

     const std::string& get_cookie(const std::string& key) {
         return cookies_[key];
     }

     /**
      * Method used to get all headers passed with the request.
      * @return a map<string,string> containing all headers.
     **/
     const std::map<std::string, std::string, http::header_comparator>& get_headers() const {
         return headers_;
     }

     /**
      * Method used to get all footers passed with the request.
      * @return a map<string,string> containing all footers.
     **/
     const std::map<std::string, std::string, http::header_comparator>& get_footers() const {
         return footers_;
     }

     const std::map<std::string, std::string, http::header_comparator>& get_cookies() const {
         return cookies_;
     }

     /**
      * Method used to get the response code from the response
      * @return The response code
     **/
     int get_response_code() const {
         return status_code_;
     }

     void with_header(const std::string& key, const std::string& value) {
         headers_[key] = value;
     }

     void with_footer(const std::string& key, const std::string& value) {
         footers_[key] = value;
     }

     void with_cookie(const std::string& key, const std::string& value) {
         cookies_[key] = value;
     }

     void shoutCAST();

     virtual MHD_Response* get_raw_response();
     virtual void decorate_response(MHD_Response* response);
     virtual int enqueue_response(MHD_Connection* connection, MHD_Response* response);

 private:
     int status_code_ = -1;

     http::header_map headers_;
     http::header_map footers_;
     http::header_map cookies_;

     // SBO state for the polymorphic body. body_ is either nullptr (no
     // body), a pointer into body_storage_ (inline), or a heap pointer
     // allocated via ::operator new(sizeof(T)) + placement-new (heap
     // fallback). body_inline_ discriminates the two non-null cases so
     // the destructor knows whether to invoke ::operator delete.
     // kind_ lets dispatch sites fast-path on body kind without a
     // virtual call.
     body_kind kind_ = body_kind::empty;
     alignas(16) std::byte body_storage_[body_buf_size]{};
     detail::body* body_ = nullptr;
     bool body_inline_ = false;

     // SBO lifecycle helpers shared by destructor / move ctor /
     // move-assign. Both noexcept (DR-005). See http_response.cpp for
     // the inline-vs-heap discriminator details.
     void destroy_body() noexcept;
     void adopt_body_from(http_response& o) noexcept;

     // Placement-new a concrete detail::body subclass into the SBO
     // buffer (or, if T does not fit, onto the heap via the matched
     // ::operator new(sizeof(T))/::operator delete pairing the
     // destructor relies on). Defined out-of-line in http_response.cpp
     // because it requires the complete type detail::body — it is only
     // instantiated from the factory bodies in that TU.
     //
     // Pre-condition: the response's body slot is empty
     // (default-constructed). Factories construct on a fresh
     // http_response, so this always holds; an assertion guards it.
     template <typename T, typename... Args>
     void emplace_body(body_kind k, Args&&... args);

 protected:
     friend std::ostream &operator<< (std::ostream &os, const http_response &r);

     // The TASK-009 SBO unit test exercises the four-case move
     // cross-product directly through the SBO state above. Only the test
     // TU is friended; production callers go through the (forthcoming
     // TASK-010) factory functions. The friend is restricted by name and
     // does not widen the public API.
     friend struct http_response_sbo_test_access;
};

std::ostream &operator<<(std::ostream &os, const http_response &r);

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_RESPONSE_HPP_
