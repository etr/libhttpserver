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

// TASK-048: lifecycle-hook alias installation for the three v1-derived
// single-slot setters (auth_handler, not_found_handler,
// method_not_allowed_handler).
//
// Each setter, when non-null on the create_webserver builder, registers
// one hook at the matching phase:
//
//   - not_found_handler          -> hook_phase::route_resolved
//     Observation-only per DR-012 §4.10: route_resolved_ctx does not carry
//     a mutable response slot, so the 404 synthesis remains in finalize_answer
//     via not_found_page(). The hook seats the alias in the bus so its count
//     is observable via the hook API.
//
//   - method_not_allowed_handler -> hook_phase::before_handler (FUNCTIONAL)
//     Checks ctx.resource->is_allowed(ctx.method). If the method is not
//     allowed, calls method_not_allowed_handler, appends the Allow header,
//     and returns hook_action::respond_with(...) to short-circuit dispatch.
//     When a functional hook fires, dispatch_resource_handler is not entered.
//
//   - auth_handler               -> hook_phase::before_handler (FUNCTIONAL)
//     Registered before method_not_allowed so auth fires first (DR-012 §4.10
//     ordering: auth alias is hook[0], method_not_allowed alias is hook[1]).
//     Respects auth_skip_paths, calls auth_handler(req), and returns
//     hook_action::respond_with(*resp) if auth fails. Replaces the former
//     apply_auth_short_circuit inline path in finalize_answer.
//
// Registration is conditional (only when the user supplied a non-null
// callable). Users who never call any of the three setters observe zero
// default hooks -- the zero-cost-when-unused invariant (PRD-HOOK-REQ-008)
// holds.
//
// Doxygen note: each setter explicitly states "This is an alias. Calling it
// registers a hook at <phase>." per TASK-048 action item.

#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "httpserver/create_webserver.hpp"
#include "httpserver/hook_action.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/http_method.hpp"
#include "httpserver/http_request.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"

namespace httpserver {

namespace {

// TASK-049: build the std::function stored in
// webserver_impl::handler_exception_alias_ from a (non-null) user-supplied
// internal_error_handler callable. Extracted from
// install_default_alias_hooks_ to keep that function under the CCN bar.
std::function<hook_action(const handler_exception_ctx&)>
make_internal_error_alias_(internal_error_handler_t user_handler) {
    return [user_handler = std::move(user_handler)](
                   const handler_exception_ctx& ctx) -> hook_action {
        if (ctx.request == nullptr) {
            // Defensive: caller always passes a non-null request, but
            // stay benign if a future call site changes that contract.
            return hook_action::pass();
        }
        return hook_action::respond_with(
            user_handler(*ctx.request, ctx.message));
    };
}

// TASK-049: install the internal_error_handler alias into the dedicated
// last-position slot on webserver_impl. Extracted from
// install_default_alias_hooks_ so the added `if` does not push the host
// function over the CCN bar. See webserver_impl::handler_exception_alias_
// for the lifetime contract (write-once-at-construction).
void install_internal_error_alias_(
        detail::webserver_impl* impl,
        internal_error_handler_t user_handler) {
    if (user_handler == nullptr) return;
    impl->handler_exception_alias_ =
        make_internal_error_alias_(std::move(user_handler));
}

// SECURITY (CWE-117): sanitize a string_view for use in an access-log line.
// Replace any ASCII control character (< 0x20 or == 0x7F) with '-' to
// prevent a client from injecting additional log lines via embedded newlines
// or carriage-returns in the request path or method. Appends directly to
// `out` rather than returning a heap-allocated copy, avoiding an extra
// std::string allocation on every request.
static void append_sanitized(std::string& out, std::string_view sv) {
    for (unsigned char c : sv) {
        out += (c < 0x20 || c == 0x7f) ? '-' : static_cast<char>(c);
    }
}

// TASK-050: install the log_access alias into the dedicated response_sent
// alias slot on webserver_impl. Extracted from install_default_alias_hooks_
// for the same reason as install_internal_error_alias_: keeping the host
// function under the project CCN gate. See webserver_impl::log_access_alias_
// for the lifetime contract.
void install_log_access_alias_(
        detail::webserver_impl* impl,
        log_access_ptr user_logger) {
    if (user_logger == nullptr) return;
    impl->log_access_alias_ =
        [user_logger = std::move(user_logger)](
                const response_sent_ctx& ctx) {
        if (ctx.request == nullptr) return;
        std::string line;
        line.reserve(64);
        // Append path and method with control-character sanitization
        // (CWE-117). Append string_view directly to avoid intermediate
        // heap allocations (performance: saves two alloc/dealloc pairs
        // per request vs. the previous std::string(sv) approach).
        append_sanitized(line, ctx.request->get_path());
        line += " METHOD: ";
        append_sanitized(line, ctx.request->get_method());
        user_logger(line);
    };
}

// Serialize an allowed-method set into the comma-separated value
// expected by the HTTP `Allow:` header. Enum-declaration order:
// GET, HEAD, POST, PUT, DELETE, CONNECT, OPTIONS, TRACE, PATCH.
// Mirrors webserver_impl::serialize_allow_methods without the impl
// dependency -- allows the alias lambda to build the Allow header
// without capturing webserver_impl*.
std::string serialize_allow_methods_local(method_set allowed) {
    std::string header_value;
    for (std::uint8_t i = 0;
            i < static_cast<std::uint8_t>(http_method::count_); ++i) {
        auto m = static_cast<http_method>(i);
        if (!allowed.contains(m)) continue;
        if (!header_value.empty()) header_value += ", ";
        header_value += std::string(to_string(m));
    }
    return header_value;
}

}  // namespace

void webserver::install_default_alias_hooks_() {
    // ----------------------------------------------------------------
    // auth_handler -> before_handler (registered first per DR-012 §4.10).
    //
    // This is an alias. Calling auth_handler(fn) registers a hook at
    // hook_phase::before_handler. Equivalent to
    // ws.add_hook(hook_phase::before_handler, ...).
    //
    // The hook replicates and replaces the former
    // webserver_impl::apply_auth_short_circuit inline path: it respects
    // auth_skip_paths, calls the user-supplied auth_handler, and
    // short-circuits with the returned response when auth fails.
    //
    // Design note (security-reviewer-iter1-7 / CWE-200): auth_handler
    // is registered as a before_handler hook, which fires only for
    // matched routes (found==true). Requests to unregistered paths
    // receive a 404 without consulting auth_handler. This means 401 vs
    // 404 distinguishes registered from unregistered routes (auth oracle).
    // Callers who need uniform authentication on all requests — including
    // 404s — should add a catch-all fallback route or register a
    // not_found_handler that applies equivalent auth logic.
    // ----------------------------------------------------------------
    if (auth_handler != nullptr) {
        // Capture both the webserver* (for auth_handler callable) and the
        // webserver_impl* (for should_skip_auth, which normalises the path
        // before comparing against auth_skip_paths).
        webserver* ws_ptr = this;
        detail::webserver_impl* impl_ptr = impl_.get();
        std::move(add_hook(hook_phase::before_handler,
            std::function<hook_action(before_handler_ctx&)>(
                [ws_ptr, impl_ptr](before_handler_ctx& ctx) -> hook_action {
                    if (ctx.request == nullptr) return hook_action::pass();
                    // Respect auth_skip_paths: skip auth for listed prefixes.
                    std::string path(ctx.request->get_path());
                    if (impl_ptr->should_skip_auth(path)) {
                        return hook_action::pass();
                    }
                    // Call the user-supplied auth_handler. Returns nullptr
                    // to mean "allow" (pass-through); non-null means
                    // "reject with this response". The shared_ptr<> is the
                    // v1 nullable-optional pattern; see the TODO on
                    // auth_handler_ptr in create_webserver.hpp for the
                    // deferred migration plan.
                    std::shared_ptr<http_response> auth_rejection_response =
                        ws_ptr->auth_handler(*ctx.request);
                    if (auth_rejection_response == nullptr) {
                        return hook_action::pass();
                    }
                    // Auth failed: short-circuit with the rejection response.
                    return hook_action::respond_with(
                        std::move(*auth_rejection_response));
                })))
            .detach();
    }

    // ----------------------------------------------------------------
    // method_not_allowed_handler -> before_handler (registered second).
    //
    // This is an alias. Calling method_not_allowed_handler(fn) registers
    // a hook at hook_phase::before_handler. Equivalent to
    // ws.add_hook(hook_phase::before_handler, ...).
    //
    // The hook checks whether the request method is in the resource's
    // allowed set. If not, it calls method_not_allowed_handler (or
    // synthesises a default 405 body), appends the Allow header, and
    // returns hook_action::respond_with(...) to short-circuit dispatch.
    // ----------------------------------------------------------------
    if (method_not_allowed_handler != nullptr) {
        webserver* ws_ptr = this;
        std::move(add_hook(hook_phase::before_handler,
            std::function<hook_action(before_handler_ctx&)>(
                [ws_ptr](before_handler_ctx& ctx) -> hook_action {
                    // Only fire when the resource is known (route hit).
                    if (ctx.resource == nullptr) {
                        return hook_action::pass();
                    }
                    if (ctx.resource->is_allowed(ctx.method)) {
                        return hook_action::pass();
                    }
                    // Method not allowed: build the response.
                    http_response resp =
                        ws_ptr->method_not_allowed_handler(*ctx.request);
                    // Append Allow header from the matched route descriptor.
                    if (ctx.matched) {
                        std::string allow_value =
                            serialize_allow_methods_local(ctx.matched->methods);
                        if (!allow_value.empty()) {
                            resp.with_header(
                                http::http_utils::http_header_allow,
                                allow_value);
                        }
                    }
                    return hook_action::respond_with(std::move(resp));
                })))
            .detach();
    }

    // ----------------------------------------------------------------
    // not_found_handler -> route_resolved (observation-only per DR-012).
    //
    // This is an alias. Calling not_found_handler(fn) registers a hook
    // at hook_phase::route_resolved. Equivalent to
    // ws.add_hook(hook_phase::route_resolved, ...).
    //
    // Structural note: route_resolved_ctx does not carry a mutable
    // response slot, so the 404 synthesis for user-provided handlers
    // remains in webserver_impl::not_found_page (called from
    // finalize_answer). The hook registers the alias in the bus so the
    // hook count is observable and the architectural seat is reserved.
    // ----------------------------------------------------------------
    if (not_found_handler != nullptr) {
        std::move(add_hook(hook_phase::route_resolved,
            std::function<void(const route_resolved_ctx&)>(
                [](const route_resolved_ctx&) {
                    // Observation stub. The actual 404 synthesis lives
                    // in webserver_impl::not_found_page, consulted from
                    // finalize_answer at the existing v1 call site.
                    // Route_resolved_ctx does not carry mr->response so
                    // the spec's "stash into mr->response" is structurally
                    // deferred (see TASK-048 spec §action item 3 note).
                })))
            .detach();
    }

    // ----------------------------------------------------------------
    // internal_error_handler -> handler_exception alias slot (LAST position).
    // [TASK-049]
    //
    // This is an alias. Calling internal_error_handler(fn) on the builder
    // makes the user callable the LAST-position fallback in the
    // handler_exception chain (DR-012 §4.10, PRD-HOOK-REQ-009).
    //
    // Unlike auth_handler / method_not_allowed_handler / not_found_handler
    // (which install at the FIRST position via add_hook so they short-
    // circuit before user hooks), the internal_error_handler alias must
    // fire LAST so user-added handler_exception hooks have a chance to
    // recover first. We achieve this by storing the alias in the
    // dedicated webserver_impl::handler_exception_alias_ slot rather than
    // push_back-ing into the hooks_handler_exception_ vector. The fire
    // site (fire_handler_exception in src/hook_handle.cpp) iterates the
    // user vector first and only then invokes the alias slot.
    //
    // The alias body invokes the user-supplied callable with the
    // originating exception's message and returns
    // hook_action::respond_with(response). If the user callable itself
    // throws, fire_handler_exception's catch arm absorbs it and returns
    // nullopt; the caller in dispatch_resource_handler then emits the
    // hardcoded empty-body 500 DIRECTLY without re-invoking the user
    // callable (it has already been seen to throw on this request --
    // calling it a second time would observably invoke the user code
    // twice for one logical exception). See webserver_dispatch.cpp.
    install_internal_error_alias_(impl_.get(), internal_error_handler);

    // ----------------------------------------------------------------
    // log_access -> response_sent alias slot. [TASK-050]
    //
    // This is an alias. Calling log_access(fn) on the create_webserver
    // builder wires `fn` into the dedicated single-slot member
    // webserver_impl::log_access_alias_, which fire_response_sent
    // invokes AFTER the user-added response_sent vector. Users who want
    // the structured ctx (status, bytes_queued, elapsed -- the data
    // issues #281 and #69 asked for) should call
    // add_hook(hook_phase::response_sent, ...) directly.
    //
    // Format compatibility: the pre-TASK-050 access log was emitted by
    // webserver_impl::access_log(parent, complete_uri + " METHOD: " +
    // method) at request-arrival time from inside answer_to_connection.
    // The existing log_access_callback integ test (test/integ/basic.cpp)
    // asserts the substrings "/logtest" AND "METHOD" appear in the
    // logged line. To keep that test passing without modification we
    // emit a line that contains both, formatted as:
    //     <path> METHOD: <method>
    // using ctx.request->get_path() and ctx.request->get_method(). The
    // alias body is null-safe (early-returns if ctx.request is null),
    // which is structurally impossible at the fire site but the guard
    // costs nothing and documents the contract.
    install_log_access_alias_(impl_.get(), log_access);
}

}  // namespace httpserver
