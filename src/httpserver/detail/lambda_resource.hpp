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

// TASK-025/TASK-026: dispatch shim used by webserver::on_methods_ to
// slot lambda handlers into the existing v1-shaped route table.
//
// The shim is a sub-class of http_resource that holds one slot per
// http_method enumerator. Its render_* virtuals look up the slot for
// the dispatched method and invoke it. The shim starts with EVERY
// method disallowed (`disallow_all()`); each on_*/route call enables
// exactly the matching bits via `set_allowing(method, true)`. The
// existing finalize_answer dispatch glue therefore returns 405 for
// unregistered methods automatically — no edit to webserver.cpp's
// dispatch path is needed.
//
// `final` is intentional: the conflict check in webserver::on_methods_
// uses dynamic_pointer_cast<lambda_resource>(...) to distinguish
// lambda-owned routes from class-owned routes. A subclass would hide
// in that test and break the invariant.
//
// Internal header — only reachable when compiling libhttpserver. NOT
// included from the public umbrella <httpserver.hpp>.
#if !defined(HTTPSERVER_COMPILATION)
#error "lambda_resource.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_LAMBDA_RESOURCE_HPP_
#define SRC_HTTPSERVER_DETAIL_LAMBDA_RESOURCE_HPP_

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include "httpserver/http_method.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/detail/route_entry.hpp"

namespace httpserver {
class http_request;
}  // namespace httpserver

namespace httpserver {
namespace detail {

// The per-method slot signature for lambda_resource. Returns
// http_response by value (DR-004) and takes the request by const
// reference. std::function is the chosen storage so users can pass any
// callable (lambda, function pointer, std::bind result, member-function
// adaptor) without leaking the concrete callable type into the slot
// array.
//
// TASK-071 history: this typedef formerly lived in route_entry.hpp as
// the lambda arm of `std::variant<lambda_handler, shared_ptr<...>>`. The
// variant arm was dead code (no writer stored a lambda directly — every
// on_*/route path funnels through lambda_resource), so it was removed.
// The typedef relocates here, where its remaining uses are.
using lambda_handler = std::function<::httpserver::http_response(const ::httpserver::http_request&)>;

// Tiny adapter that wraps a single lambda_handler as an http_resource
// virtual override. We keep one slot per method enum and dispatch in
// each render_* override. Each slot holds a
//   std::function<http_response(const http_request&)>
// (the `lambda_handler` typedef defined above). Slots are installed
// via set_slot() and invoked by the render_* overrides below.
class lambda_resource final : public ::httpserver::http_resource {
 public:
    lambda_resource() {
        // Lambda routes are opt-in per method (the default
        // http_resource constructor enables every method via
        // set_all()). on_* sets the matching bit when populating a slot.
        disallow_all();
    }

    // Install (or replace) the slot for `method`. Caller must have
    // already verified that no slot is currently set for `method`
    // (webserver::on_methods_ enforces this and throws on conflict).
    void set_slot(http_method method, lambda_handler h) {
        slots_[static_cast<std::size_t>(method)] = std::move(h);
        set_allowing(method, true);
    }

    bool has_slot(http_method method) const noexcept {
        return is_allowed(method);
    }

    // These seven overrides correspond to the seven on_* entry points exposed
    // by webserver (get, post, put, delete, patch, options, head). Each is a
    // mechanical delegation to invoke_() and differs only by the http_method
    // enum constant forwarded. They cannot be collapsed further without
    // changing the http_resource base-class interface.
    //
    // render_connect and render_trace are intentionally NOT overridden here:
    // no on_connect / on_trace API is offered, so those slots are never
    // populated. The is_allowed gate in finalize_answer prevents dispatch from
    // ever reaching the base-class render_connect / render_trace on a
    // lambda_resource shim (disallow_all() in the constructor clears the full
    // mask). The claim that "these seven are required by the base-class
    // interface" was misleading — the base class has nine render_* overrides;
    // seven are wired here by deliberate design choice, not interface mandate.
    ::httpserver::http_response
    render_get(const ::httpserver::http_request& r) override {
        return invoke_(http_method::get, r);
    }
    ::httpserver::http_response
    render_post(const ::httpserver::http_request& r) override {
        return invoke_(http_method::post, r);
    }
    ::httpserver::http_response
    render_put(const ::httpserver::http_request& r) override {
        return invoke_(http_method::put, r);
    }
    ::httpserver::http_response
    render_delete(const ::httpserver::http_request& r) override {
        return invoke_(http_method::del, r);
    }
    ::httpserver::http_response
    render_patch(const ::httpserver::http_request& r) override {
        return invoke_(http_method::patch, r);
    }
    ::httpserver::http_response
    render_options(const ::httpserver::http_request& r) override {
        return invoke_(http_method::options, r);
    }
    ::httpserver::http_response
    render_head(const ::httpserver::http_request& r) override {
        return invoke_(http_method::head, r);
    }

 private:
    // Dispatch to the registered slot for method `m`, returning the
    // handler's http_response by value. Callers must ensure is_allowed(m)
    // is true before calling (the 405 guard in finalize_answer enforces this).
    ::httpserver::http_response
    invoke_(http_method m, const ::httpserver::http_request& r) {
        auto& slot = slots_[static_cast<std::size_t>(m)];
        // Invariant: set_slot stores the handler AND calls
        // set_allowing(method, true); the finalize_answer 405 path fires
        // before reaching invoke_ unless has_slot is true. A populated
        // slot is therefore guaranteed here.
        //
        // assert fires in debug builds; the explicit check beneath it is
        // the defensive release-build path (CWE-617: assert compiled away
        // under NDEBUG). An empty std::function would otherwise throw
        // std::bad_function_call, which is undefined behavior inside an
        // MHD callback. The check branch is predicted-not-taken and has
        // negligible runtime cost.
        assert(slot);
        if (!slot) {
            return ::httpserver::http_response::string(
                "Internal Server Error: unregistered method slot invoked")
                .with_status(500);
        }
        // TASK-036: lambda_handler returns http_response by value; the
        // dispatch path in webserver_impl::finalize_answer moves the
        // returned value into modded_request::response — the single
        // anchor that keeps deferred-body producers alive until
        // request_completed (DR-010, §5.3).
        return slot(r);
    }

    std::array<lambda_handler,
               static_cast<std::size_t>(http_method::count_)> slots_{};
};

}  // namespace detail
}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_LAMBDA_RESOURCE_HPP_
