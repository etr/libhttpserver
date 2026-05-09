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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

#if !defined (_HTTPSERVER_HPP_INSIDE_) && !defined (HTTPSERVER_COMPILATION)
#error "Only <httpserver.hpp> or <httpserverpp> can be included directly."
#endif

#ifndef SRC_HTTPSERVER_HTTP_RESOURCE_HPP_
#define SRC_HTTPSERVER_HTTP_RESOURCE_HPP_

#include <memory>

#include "httpserver/http_method.hpp"

namespace httpserver { class http_request; }
namespace httpserver { class http_response; }
namespace httpserver { class webserver; }
namespace httpserver { namespace detail { class webserver_impl; } }

namespace httpserver {

namespace detail { std::shared_ptr<http_response> empty_render(const http_request& r); }

/**
 * Class representing a callable http resource.
**/
class http_resource {
 public:
     /**
      * Class destructor
     **/
     virtual ~http_resource() = default;

     /**
      * Method used to answer to a generic request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render(const http_request& req) {
         return detail::empty_render(req);
     }

     /**
      * Method used to answer to a GET request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_get(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a POST request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_post(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PUT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_put(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a HEAD request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_head(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a DELETE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_delete(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a TRACE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_trace(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a OPTIONS request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_options(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PATCH request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_patch(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a CONNECT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual std::shared_ptr<http_response> render_connect(const http_request& req) {
         return render(req);
     }

     /**
      * Toggle whether a specific http_method is allowed on this resource.
      * @param method enum identifying the method (no string lookup)
      * @param allow true to enable the method, false to disable it
     **/
     void set_allowing(http_method method, bool allow) noexcept {
         if (method == http_method::count_) return;  // sentinel; never settable
         if (allow) {
             methods_allowed_.set(method);
         } else {
             methods_allowed_.clear(method);
         }
     }

     /**
      * Allow every defined http_method on this resource.
     **/
     void allow_all() noexcept {
         methods_allowed_.set_all();
     }

     /**
      * Disallow every http_method on this resource.
     **/
     void disallow_all() noexcept {
         methods_allowed_.clear_all();
     }

     /**
      * Test whether `method` is allowed on this resource. Const-noexcept
      * because the answer is a single bitmask test on a trivial member;
      * no string lookup, no allocation.
      * @param method enum identifying the method to query
      * @return true if the method is currently allowed
     **/
     bool is_allowed(http_method method) const noexcept {
         return methods_allowed_.contains(method);
     }

     /**
      * Return the full allow-mask by value. The returned method_set is
      * trivially copyable (sizeof == 4) so by-value is the natural ABI.
     **/
     method_set get_allowed_methods() const noexcept {
         return methods_allowed_;
     }

 protected:
     /**
      * Constructor of the class. The default state allows every defined
      * http_method, matching the v1 behaviour where `resource_init`
      * marked all nine methods true.
     **/
     http_resource() = default;

     /**
      * Copy / move special members are trivial — the only data member
      * is method_set (a 32-bit aggregate).
     **/
     http_resource(const http_resource& b) = default;
     http_resource(http_resource&& b) noexcept = default;
     http_resource& operator=(const http_resource& b) = default;
     http_resource& operator=(http_resource&& b) = default;

 private:
     friend class webserver;
     friend class detail::webserver_impl;  // TASK-014: dispatch helpers

     // Default-allow every valid method. method_set::set_all() is
     // constexpr, so the chained call is a constant expression and the
     // default member initialiser stays well-formed.
     method_set methods_allowed_ = method_set{}.set_all();
};

// TASK-021 acceptance: http_resource is now a vptr plus a 32-bit
// method_set plus padding. The cap below leaves headroom for one
// future small member (e.g. an arena tag) without re-invalidating
// PRD-REQ-REQ-002 / PRD-REQ-REQ-003.
static_assert(sizeof(http_resource) <= sizeof(void*) + sizeof(method_set) * 2,
              "http_resource should be approximately vptr + method_set");

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_RESOURCE_HPP_
