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

// TASK-118 step 3: the dispatch pipeline driver (plan D3/D4). The
// branch helpers live in the anonymous namespace, each small enough
// for the CCN gate; every value-shaped commit reuses the shared
// commit_sync_value tail (auto Content-Length), and the v2 default
// pages carry the v2 bodies ("Not Found", "Method not Allowed",
// text/plain).

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/detail/exchange_runner.hpp>
#include <httpserver/detail/lifecycle_sink.hpp>
#include <httpserver/detail/request_lifecycle.hpp>
#include <httpserver/server/route_sync.hpp>

namespace httpserver {

namespace detail {

namespace {

using resolve_result = server::route_registry::resolve_result;
using resolve_kind = resolve_result::resolve_kind;

constexpr std::string_view k_not_found_body = "Not Found";
constexpr std::string_view k_method_not_allowed_body = "Method not Allowed";

// The Allow wire form of a resolve outcome: the known-slot set in
// method_id order plus any extension singles.
std::string render_allow(const resolve_result& resolved) {
    std::string allow = http::to_string(resolved.methods);
    for (const std::string& name : resolved.extension_names) {
        if (!allow.empty()) allow.append(", ");
        allow.append(name);
    }
    return allow;
}

// The hook-visible route of a resolve outcome. The pattern view is
// valid while the resolve_result lives (the dispatcher holds it across
// the fires).
server::route_descriptor describe(const resolve_result& resolved) {
    server::route_descriptor route;
    route.pattern = resolved.pattern_text;
    route.methods = resolved.methods;
    route.is_prefix = resolved.is_prefix;
    return route;
}

server::sync_response as_value(server::hook_response page) {
    return server::sync_response{page.status, std::move(page.fields),
                                 std::move(page.body)};
}

// Appends Allow when the response does not carry one (every 405 rides
// Allow, custom or default).
void append_allow(server::hook_response& page, const std::string& allow) {
    if (page.fields.first("allow") == std::nullopt) {
        page.fields.append("Allow", allow);
    }
}

// One value-shaped commit through the interceptor with its provenance
// (the shared auto-Content-Length tail frames it).
task<void> commit_value(exchange& x, lifecycle_sink& sink,
                        lifecycle_sink::provenance origin,
                        server::sync_response value) {
    sink.begin(origin);
    co_await server::detail::commit_sync_value(x, std::move(value));
}

// One synthesized error page: the custom factory's response when
// installed, else the v2 default (status, text/plain, fixed body).
server::hook_response build_page(
        const server::server_options::response_factory* custom,
        const http::request_head& head, std::uint16_t code,
        std::string_view default_body) {
    if (custom != nullptr) return (*custom)(head);
    server::hook_response page;
    page.status = http::status::from_code(code);
    page.fields.append("Content-Type", "text/plain");
    const std::byte* raw =
        reinterpret_cast<const std::byte*>(default_body.data());
    page.body.assign(raw, raw + default_body.size());
    return page;
}

task<void> commit_page(exchange& x, lifecycle_sink& sink,
                       const server::server_options::response_factory* custom,
                       std::uint16_t code, std::string_view default_body,
                       const std::string* allow) {
    server::hook_response page =
        build_page(custom, x.head(), code, default_body);
    if (allow != nullptr) append_allow(page, *allow);
    co_await commit_value(x, sink, lifecycle_sink::provenance::synthesis,
                          as_value(std::move(page)));
}

// The bare synthesized 500/501 (the pre-hook runner's shape: empty
// fields, one-shot commit).
void commit_bare(exchange& x, lifecycle_sink& sink, std::uint16_t code) {
    sink.begin(lifecycle_sink::provenance::synthesis);
    commit_error(x, code);
}

// The handler_exception chain: a hook may supply the response, else
// the synthesized 500. On a post-commit throw the commit attempt below
// fails quietly (the engine already owns the connection's fate).
task<void> rescue_exception(const server::hook_bus& bus, lifecycle_sink& sink,
                            std::exception_ptr error, exchange& x) {
    if (bus.any_hooks(server::hook_phase::handler_exception)) {
        server::handler_exception_ctx ctx{x.head(), error};
        server::hook_action action =
            bus.fire<server::hook_phase::handler_exception>(ctx);
        if (!action.is_pass()) {
            server::hook_response page = std::move(action).take_response();
            if (page.status.valid()) {
                co_await commit_value(
                    x, sink, lifecycle_sink::provenance::exception_supply,
                    as_value(std::move(page)));
                co_return;
            }
        }
    }
    commit_bare(x, sink, 500);
}

// Invokes the handler and owns its exception paths.
task<void> run_handler(const server::hook_bus& bus, lifecycle_sink& sink,
                       const server::route_handler& handler, exchange& x) {
    // co_await is illegal inside a catch handler; the exception ptr is
    // captured here and the rescue runs after the handler.
    std::exception_ptr error;
    try {
        co_await invoke_route_handler(handler, x);
    } catch (const cancelled_exception&) {
        // Disconnect cancellation is a quiet end; the tail reports the
        // failure verdict.
        co_return;
    } catch (...) {
        if (x.terminal()) static_cast<void>(x.abort());
        error = std::current_exception();
    }
    if (error) {
        co_await rescue_exception(bus, sink, error, x);
        co_return;
    }
    if (!x.terminal()) commit_bare(x, sink, 500);
}

// The miss branch: the 404 page.
task<void> dispatch_miss(const error_page_factories& pages,
                         lifecycle_sink& sink, exchange& x) {
    co_await commit_page(x, sink, pages.not_found ? &pages.not_found : nullptr,
                         404, k_not_found_body, nullptr);
}

// The method-miss branch: before_handler is the consultation point (a
// hook may supply the 405; Allow always rides), else the page.
task<void> dispatch_method_miss(const server::hook_bus& bus,
                                const error_page_factories& pages,
                                const resolve_result& resolved,
                                lifecycle_sink& sink, exchange& x) {
    const std::string allow = render_allow(resolved);
    if (bus.any_hooks(server::hook_phase::before_handler)) {
        server::before_handler_ctx ctx{x.head(), describe(resolved)};
        server::hook_action action =
            bus.fire<server::hook_phase::before_handler>(ctx);
        if (!action.is_pass()) {
            server::hook_response page = std::move(action).take_response();
            if (page.status.valid()) {
                append_allow(page, allow);
                co_await commit_value(
                    x, sink, lifecycle_sink::provenance::pre_handler,
                    as_value(std::move(page)));
                co_return;
            }
        }
    }
    co_await commit_page(x, sink,
                         pages.method_not_allowed
                             ? &pages.method_not_allowed
                             : nullptr,
                         405, k_method_not_allowed_body, &allow);
}

// The hit branch: before_handler may short-circuit, else the handler
// runs with its captures stamped as path args.
task<void> dispatch_hit(const server::hook_bus& bus,
                        resolve_result& resolved, lifecycle_sink& sink,
                        exchange& x) {
    if (bus.any_hooks(server::hook_phase::before_handler)) {
        server::before_handler_ctx ctx{x.head(), describe(resolved)};
        server::hook_action action =
            bus.fire<server::hook_phase::before_handler>(ctx);
        if (!action.is_pass()) {
            server::hook_response page = std::move(action).take_response();
            if (page.status.valid()) {
                co_await commit_value(
                    x, sink, lifecycle_sink::provenance::pre_handler,
                    as_value(std::move(page)));
                co_return;
            }
        }
    }
    x.set_path_args(std::move(resolved.captures));
    co_await run_handler(bus, sink, *resolved.handler, x);
}

// request_received: the first short-circuit point. True when the hook
// supplied a response (an invalid status is treated as a pass).
task<bool> short_circuit_request_received(const server::hook_bus& bus,
                                          lifecycle_sink& sink, exchange& x) {
    if (!bus.any_hooks(server::hook_phase::request_received)) {
        co_return false;
    }
    server::request_received_ctx ctx{x.head()};
    server::hook_action action =
        bus.fire<server::hook_phase::request_received>(ctx);
    if (action.is_pass()) co_return false;
    server::hook_response page = std::move(action).take_response();
    if (!page.status.valid()) co_return false;
    co_await commit_value(x, sink, lifecycle_sink::provenance::pre_handler,
                          as_value(std::move(page)));
    co_return true;
}

void fire_route_resolved(const server::hook_bus& bus, const exchange& x,
                         const resolve_result& resolved) {
    if (!bus.any_hooks(server::hook_phase::route_resolved)) return;
    server::route_resolved_ctx ctx{
        x.head(), resolved.kind == resolve_kind::hit, describe(resolved)};
    // Observation-only: the action is ignored by contract.
    (void)bus.fire<server::hook_phase::route_resolved>(ctx);
}

// The exactly-once tail. succeeded follows the v2 mapping: true when a
// complete response (or upgrade) reached the engine and nothing forced
// the exchange down afterwards; false with the typed end reason
// otherwise.
// The typed end reason of a failed settle: the exchange's disconnect
// reason when one is known, else a generic failure.
http::outcome end_reason_of(const exchange& x) {
    if (x.disconnected()) return x.disconnect_reason();
    return http::outcome(http::outcome_code::invalid_state,
                         "request_lifecycle: the exchange ended without a"
                         " terminal decision");
}

void fire_completed(const server::hook_bus& bus, const exchange& x,
                    const lifecycle_sink& sink) {
    if (!bus.any_hooks(server::hook_phase::request_completed)) return;
    const bool settled_clean =
        x.terminal() && !x.disconnected() && !sink.aborted();
    server::request_completed_ctx ctx{
        x.head(), settled_clean,
        settled_clean ? http::outcome::okay() : end_reason_of(x)};
    (void)bus.fire<server::hook_phase::request_completed>(ctx);
}

}  // namespace

task<void> dispatch_request(const server::route_registry& routes,
                            const server::hook_bus& bus,
                            const error_page_factories& pages,
                            lifecycle_sink& sink, exchange& x) {
    // A peer that is already gone answers nothing; the tail still
    // reports the settle.
    if (x.disconnected()) {
        fire_completed(bus, x, sink);
        co_return;
    }
    // Defensive: a head without a valid method never matches a
    // registration; answer 501 instead of reporting a miss.
    if (!x.head().request_method.valid()) {
        commit_bare(x, sink, 501);
        fire_completed(bus, x, sink);
        co_return;
    }
    if (co_await short_circuit_request_received(bus, sink, x)) {
        fire_completed(bus, x, sink);
        co_return;
    }
    resolve_result resolved =
        routes.resolve(x.head().request_method, x.head().route_path);
    fire_route_resolved(bus, x, resolved);
    switch (resolved.kind) {
        case resolve_kind::miss:
            co_await dispatch_miss(pages, sink, x);
            break;
        case resolve_kind::method_miss:
            co_await dispatch_method_miss(bus, pages, resolved, sink, x);
            break;
        case resolve_kind::hit:
            co_await dispatch_hit(bus, resolved, sink, x);
            break;
    }
    fire_completed(bus, x, sink);
}

}  // namespace detail

}  // namespace httpserver
