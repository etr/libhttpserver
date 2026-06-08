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

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "httpserver/create_webserver.hpp"
#include "httpserver/detail/method_utils.hpp"
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

// TASK-049: install the internal_error_handler alias into the dedicated
// last-position slot on webserver_impl. Extracted from
// install_default_alias_hooks_ so the added `if` does not push the host
// function over the CCN bar. See webserver_impl::handler_exception_alias_
// for the lifetime contract (write-once-at-construction).
//
// Naming: no trailing underscore -- this is a file-scope free function in
// an anonymous namespace, not a private member function. Matches the
// naming convention of install_log_access_alias (which itself follows
// the same pattern). (TASK-049 review findings #20/#22.)
//
// Also sets any_hooks_[handler_exception] so the gate is the single source
// of truth even when only the alias is wired (finding #4). A future caller
// that checks only any_hooks_ (e.g., a stats collector) then observes the
// correct true value without also needing to know about the alias slot.
void install_internal_error_alias(
        detail::webserver_impl* impl,
        internal_error_handler_t user_handler) {
    if (user_handler == nullptr) return;
    impl->handler_exception_alias_ =
        [user_handler = std::move(user_handler)](
                const handler_exception_ctx& ctx) -> hook_action {
            // mr->dhr is always non-null at the call site in
            // handle_dispatch_exception; this guard is purely defensive for
            // any future call site that relaxes that invariant. If that
            // invariant is violated in debug builds, assert fires first.
            // (Findings #5, #8, #9: assert in debug, guard stays for safety.)
            assert(ctx.request != nullptr &&
                   "handler_exception_ctx::request must be non-null");
            if (ctx.request == nullptr) return hook_action::pass();
            return hook_action::respond_with(
                user_handler(*ctx.request, ctx.message));
        };
    // Set the any_hooks_ gate so it remains the canonical zero-cost fast-
    // check for handler_exception, regardless of whether hooks are in the
    // vector or only in the alias slot (finding #4 / DR-012 §4.10).
    impl->any_hooks_[static_cast<std::size_t>(hook_phase::handler_exception)]
        .store(true, std::memory_order_release);
}

// SECURITY (CWE-117): sanitize a string_view for use in an access-log line.
// Replace any ASCII control character (< 0x20 or == 0x7F) with '-' to
// prevent a client from injecting additional log lines via embedded newlines
// or carriage-returns in the request path or method. Appends directly to
// `out` rather than returning a heap-allocated copy, avoiding an extra
// std::string allocation on every request.
void append_sanitized(std::string& out, std::string_view sv) {
    for (unsigned char c : sv) {
        out += (c < 0x20 || c == 0x7f) ? '-' : static_cast<char>(c);
    }
}

// TASK-050: install the log_access alias into the dedicated response_sent
// alias slot on webserver_impl. Extracted from install_default_alias_hooks_
// for the same reason as install_internal_error_alias_: keeping the host
// function under the project CCN gate. See webserver_impl::log_access_alias_
// for the lifetime contract.
void install_log_access_alias(
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

}  // namespace

// ----------------------------------------------------------------
// auth_handler -> before_handler (registered first per DR-012 §4.10).
//
// This is an alias. Calling auth_handler(fn) registers a hook at
// hook_phase::before_handler. Equivalent to
// ws.add_hook(hook_phase::before_handler, ...).
//
// The hook IS the auth enforcement path — it replaces (and removes)
// the former webserver_impl::apply_auth_short_circuit inline call.
// It respects auth_skip_paths via should_skip_auth, calls the user-
// supplied auth_handler callable, and returns
// hook_action::respond_with(*resp) when auth fails, short-circuiting
// the remaining before_handler chain AND dispatch_resource_handler.
//
// IMPORTANT: because before_handler fires from finalize_answer BEFORE
// dispatch_resource_handler is called (see webserver_request.cpp:
// fire_before_handler_gated), the auth hook fires for every request
// that resolves to a registered route (route hit). It does NOT fire
// for 404 paths (found==false), which is the correct semantic: there
// is no resource to authenticate against.
//
// DO NOT remove or replace this hook registration without also
// providing an equivalent enforcement mechanism. The hook IS the
// security boundary; there is no separate apply_auth_short_circuit
// fallback path remaining.
//
// Design note (security-reviewer-iter1-7 / CWE-200): the route-hit-only
// firing above creates an auth oracle — requests to unregistered paths
// get 404 without auth, while registered paths get 401 if blocked, so
// 401-vs-404 distinguishes registered from unregistered routes.
// Callers needing uniform authentication on all requests (including
// 404s) should add a catch-all fallback route or register a
// not_found_handler that applies equivalent auth logic.
void webserver::install_auth_alias_() {
    if (auth_handler == nullptr) return;
    // Capture both the webserver* (for auth_handler callable) and the
    // webserver_impl* (for should_skip_auth, which normalises the path
    // before comparing against auth_skip_paths).
    webserver* ws_ptr = this;
    detail::webserver_impl* impl_ptr = impl_.get();
    // add_hook returns a prvalue hook_handle; .detach() is called
    // directly on it -- std::move() on a prvalue is a no-op and
    // is omitted here (finding #19).
    add_hook(hook_phase::before_handler,
        std::function<hook_action(before_handler_ctx&)>(
            [ws_ptr, impl_ptr](before_handler_ctx& ctx) -> hook_action {
                if (ctx.request == nullptr) return hook_action::pass();
                // Respect auth_skip_paths: skip auth for listed prefixes.
                // Empty skip-list is the production-typical case — skip
                // the std::string allocation entirely when there is
                // nothing to compare against (TASK-054 review #21;
                // should_skip_auth's own empty-list early-out from
                // TASK-058 still fires for the non-empty path).
                if (!ws_ptr->auth_skip_paths_normalized.empty()) {
                    std::string path(ctx.request->get_path());
                    if (impl_ptr->should_skip_auth(path)) {
                        return hook_action::pass();
                    }
                }
                // Call the user-supplied auth_handler. TASK-054: the
                // return type is std::optional<http_response>. nullopt
                // means "allow"; an engaged optional carries the
                // rejection response. Compared to the v1
                // shared_ptr<http_response> shape this saves the
                // per-authenticated-request control-block allocation
                // (one heap alloc removed per request that runs through
                // this hook), and small responses ride the http_response
                // SBO with zero further allocs.
                auto rejection = ws_ptr->auth_handler(*ctx.request);
                if (!rejection) {
                    return hook_action::pass();
                }
                // Auth failed: short-circuit with the rejection response.
                return hook_action::respond_with(std::move(*rejection));
            }))
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
void webserver::install_method_not_allowed_alias_() {
    if (method_not_allowed_handler == nullptr) return;
    webserver* ws_ptr = this;
    add_hook(hook_phase::before_handler,
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
                // TASK-048 review cleanup: use the shared free function
                // detail::format_allow_header (method_utils.hpp) instead of
                // the former local duplicate serialize_allow_methods_local.
                if (ctx.matched) {
                    std::string allow_value =
                        detail::format_allow_header(ctx.matched->methods);
                    if (!allow_value.empty()) {
                        resp.with_header(
                            http::http_utils::http_header_allow,
                            allow_value);
                    }
                }
                return hook_action::respond_with(std::move(resp));
            }))
        .detach();
}

// ----------------------------------------------------------------
// not_found_handler -> route_resolved (observation-only per DR-012 §4.10).
//
// This is an alias. Calling not_found_handler(fn) registers a hook
// at hook_phase::route_resolved. Equivalent to
// ws.add_hook(hook_phase::route_resolved, ...).
//
// Structural pin: per DR-012 §4.10, route_resolved is observation-only
// — it cannot mutate the in-flight response or its delivery, and
// route_resolved_ctx exposes no mutable response slot. The user-
// provided not_found_handler is therefore consulted at the v1 call
// site webserver_impl::not_found_page (invoked from finalize_answer
// and the materialize-fallback path; see src/detail/webserver_error_pages.cpp
// and src/detail/webserver_request.cpp). The alias seat here is the
// architectural anchor: it reserves a stable hook[0] index at this
// phase (per PRD-HOOK-REQ-009), it keeps the hook count observable
// via the public hook API (verified by hooks_alias_count_test), and
// it gives future observation-only integrations (logging, metrics)
// a known phase boundary to subscribe alongside. The on-wire 404
// body shape is pinned by hooks_not_found_alias_test (default and
// custom branches) and by basic.cpp:custom_not_found_handler.
//
// TASK-071: previously labelled "structurally deferred (TASK-048)".
// TASK-048 shipped; the deferral was a misread of the design — the
// phase being observation-only is not a deferral, it IS the design
// (see DR-012 §4.10). The body remains a documented no-op observation
// marker; the forward-debt comment has been removed.
void webserver::install_not_found_alias_() {
    if (not_found_handler == nullptr) return;
    add_hook(hook_phase::route_resolved,
        std::function<void(const route_resolved_ctx&)>(
            [](const route_resolved_ctx& ctx) {
                // Pure observation marker. The on-wire 404 body is
                // synthesised by webserver_impl::not_found_page; this
                // hook intentionally does NOT re-invoke the user
                // handler (doing so would double-count the user
                // handler's call rate and violate v1 observed-call-
                // count semantics). DR-012 §4.10 forbids mutating the
                // response from this phase. The discard of `ctx` makes
                // explicit that no response-shaping decision is taken
                // here.
                (void) ctx;
            }))
        .detach();
}

void webserver::install_default_alias_hooks_() {
    install_auth_alias_();
    install_method_not_allowed_alias_();
    install_not_found_alias_();

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
    //
    // The alias slot is written exactly once here and is immutable
    // thereafter (DR-012 / §4.10). Runtime extension of the
    // handler_exception phase is via add_hook(); the alias slot is not
    // user-mutable post-construction.
    install_internal_error_alias(impl_.get(), internal_error_handler);

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
    // Format: '<path> METHOD: <method>' -- mirrors v1 access_log to keep
    // basic.cpp log_access_callback test passing without modification.
    install_log_access_alias(impl_.get(), log_access);
}

}  // namespace httpserver
