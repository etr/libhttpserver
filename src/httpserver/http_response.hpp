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

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include "httpserver/body_kind.hpp"
#include "httpserver/http_arg_value.hpp"
#include "httpserver/http_utils.hpp"

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
