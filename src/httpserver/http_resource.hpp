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

#include <functional>
#include <memory>

// TASK-036: render_* virtuals now return http_response by value; the
// inline defaults call render(req) and forward the prvalue, which
// requires http_response to be a complete type at every override site.
// Hard-include is the simplest correct shape (the umbrella already
// reaches both headers).
#include "httpserver/http_method.hpp"
#include "httpserver/http_response.hpp"

// TASK-051: add_hook overloads on http_resource return hook_handle; the
// public header is part of the umbrella surface (no MHD leak).
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"

namespace httpserver { class http_request; }
namespace httpserver { class webserver; }
namespace httpserver { namespace detail { class webserver_impl; } }
namespace httpserver { namespace detail { class resource_hook_table; } }

namespace httpserver {

// TASK-036 / DR-004 / PRD-RSP-REQ-007: render_* virtuals return
// http_response by value. The webserver dispatch path moves the value
// into mr->response_ (an std::optional<http_response> living on the
// per-connection modded_request, see §5.3) and keeps it alive until
// MHD fires request_completed. The default render() returns a
// default-constructed http_response whose status_code_ == -1 is the
// v1-compatible sentinel for "handler did not produce a response"; the
// dispatch path routes the sentinel through the internal-error handler.

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
     virtual http_response render(const http_request& req) {
         (void)req;
         // TASK-036: default-constructed http_response carries
         // status_code_ == -1 — the v1-compatible "handler did not
         // produce a response" sentinel that finalize_answer recognises
         // and routes through internal_error_page (see
         // test/integ/basic.cpp::default_render_method).
         return http_response{};
     }

     /**
      * Method used to answer to a GET request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_get(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a POST request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_post(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PUT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_put(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a HEAD request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_head(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a DELETE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_delete(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a TRACE request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_trace(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a OPTIONS request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_options(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a PATCH request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_patch(const http_request& req) {
         return render(req);
     }

     /**
      * Method used to answer to a CONNECT request
      * @param req Request passed through http
      * @return A http_response object
     **/
     virtual http_response render_connect(const http_request& req) {
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

     /**
      * TASK-051 / DR-012 / PRD-HOOK-REQ-006. Register a per-resource hook
      * for one of the five post-route-resolution phases: before_handler,
      * handler_exception, after_handler, response_sent, request_completed.
      *
      * Per-route hooks fire AFTER the server-wide chain at the same phase,
      * and only if the server-wide chain did not short-circuit. The
      * returned hook_handle owns the registration: destroying it (or
      * calling remove()) erases the entry. If the resource is destroyed
      * before the handle, the handle's destructor / remove() become no-ops.
      *
      * Passing any phase outside the five permitted ones throws
      * std::invalid_argument naming the rejected phase. Passing an empty
      * std::function also throws std::invalid_argument.
      *
      * @param phase one of: before_handler, handler_exception,
      *              after_handler, response_sent, request_completed.
      * @param fn    non-empty callable matching the phase's signature.
      * @return move-only hook_handle owning the registration.
      */
     hook_handle add_hook(hook_phase phase,
         std::function<hook_action(before_handler_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<hook_action(const handler_exception_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<hook_action(after_handler_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<void(const response_sent_ctx&)> fn);
     hook_handle add_hook(hook_phase phase,
         std::function<void(const request_completed_ctx&)> fn);

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

     // TASK-051: per-resource hook bus storage (PIMPL). Lazily allocated
     // on first add_hook() call; resources that never register a hook
     // pay zero allocation cost and only sizeof(shared_ptr) of nullptr
     // storage. shared_ptr lets the hook_handle hold a weak_ptr that
     // expires cleanly when the resource is destroyed (handle.remove()
     // then becomes a no-op without dereferencing freed memory).
     //
     // `mutable` because hook firing on `const http_resource&` (from the
     // const path through dispatch) needs to read this pointer; the
     // logical const-ness of the resource is preserved (the firing path
     // only reads the table; only the public add_hook(non-const) writes).
     mutable std::shared_ptr<detail::resource_hook_table> hook_table_;

#if defined(HTTPSERVER_COMPILATION)

 public:
     // Internal accessor for the dispatch path (webserver_impl). The
     // pointer is null when no hook has ever been registered on this
     // resource (the dispatch hot path treats null as "no per-route
     // hooks for any phase"). Public-but-HTTPSERVER_COMPILATION-gated
     // for the same reason webserver::make_hook_handle_ is: the symbol
     // is reachable only from within the library translation units.
     detail::resource_hook_table* hook_table_raw_() const noexcept {
         return hook_table_.get();
     }

 private:
#endif
};

// TASK-021 acceptance: http_resource was a vptr plus a 32-bit method_set
// plus padding. TASK-051 added one `shared_ptr<resource_hook_table>` to
// host the per-resource hook table PIMPL; the cap below reflects the
// growth (vptr + shared_ptr + method_set + padding). Documented growth,
// not silent drift -- PRD-REQ-REQ-002 / PRD-REQ-REQ-003 still hold (the
// v1 std::map cost was much larger than this single shared_ptr slot).
static_assert(sizeof(http_resource) <=
                  sizeof(void*) * 3 + sizeof(method_set) * 2,
              "http_resource should be approximately vptr + shared_ptr + "
              "method_set after TASK-051");

}  // namespace httpserver
#endif  // SRC_HTTPSERVER_HTTP_RESOURCE_HPP_
