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

// Per-dispatch bookkeeping threaded through the branch helpers: the
// handler_exception chain must fire at most once per request (D4), so
// a surfacing that already consulted the chain suppresses the
// consultation a later handler throw would otherwise run.
struct dispatch_state {
    bool exception_chain_fired = false;
};

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
// The factory is user code in the request path and gets the handler
// containment: a throwing or empty factory and a result with an
// invalid status each degrade to the default page with the error
// status preserved, so every dispatch path reaches a commit. This
// is v3 containment, not v2 parity: v2 answered a throwing 405
// alias through the internal-error path (a 500-family answer) and
// left a throwing 404 alias uncontained on the primary 404 path
// (the parity inventory records the delta under REQ-038).
server::hook_response build_page(
        const server::server_options::response_factory* custom,
        const http::request_head& head, std::uint16_t code,
        std::string_view default_body) {
    if (custom != nullptr && *custom) {
        try {
            server::hook_response page = (*custom)(head);
            if (page.status.valid()) return page;
        } catch (...) {
            // Fall through to the default page.
        }
    }
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

// The handler_exception consultation shared by the handler-throw
// rescue and the D4 surfacing: a hook may supply the response (the
// exception_supply provenance; @p allow rides the 405 branch), else
// the caller owns the fallback (the bare 500 for a handler throw,
// "continue the pipeline" for a surfaced hook error). Fires at most
// once per request (dispatch_state).
task<bool> consult_exception_chain(const server::hook_bus& bus,
                                   lifecycle_sink& sink, exchange& x,
                                   const std::string* allow,
                                   dispatch_state& st,
                                   std::exception_ptr error) {
    if (st.exception_chain_fired) co_return false;
    st.exception_chain_fired = true;
    if (!bus.any_hooks(server::hook_phase::handler_exception)) {
        co_return false;
    }
    server::handler_exception_ctx ctx{x.head(), std::move(error)};
    server::hook_action action =
        bus.fire<server::hook_phase::handler_exception>(ctx);
    if (!action.is_pass()) {
        server::hook_response page = std::move(action).take_response();
        if (page.status.valid()) {
            if (allow != nullptr) append_allow(page, *allow);
            co_await commit_value(
                x, sink, lifecycle_sink::provenance::exception_supply,
                as_value(std::move(page)));
            co_return true;
        }
    }
    co_return false;
}

// The handler-throw rescue: the chain may supply the response, else
// the bare 500. On a post-commit throw the commit attempt below fails
// quietly (the engine already owns the connection's fate).
task<void> rescue_exception(const server::hook_bus& bus, lifecycle_sink& sink,
                            dispatch_state& st, std::exception_ptr error,
                            exchange& x) {
    if (!co_await consult_exception_chain(bus, sink, x, nullptr, st,
                                          std::move(error))) {
        commit_bare(x, sink, 500);
    }
}

// Invokes the handler and owns its exception paths.
task<void> run_handler(const server::hook_bus& bus, lifecycle_sink& sink,
                       dispatch_state& st,
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
        co_await rescue_exception(bus, sink, st, std::move(error), x);
        co_return;
    }
    if (!x.terminal()) commit_bare(x, sink, 500);
}

// The miss branch: the 404 page.
task<void> dispatch_miss(const error_page_factories& pages,
                         lifecycle_sink& sink, exchange& x) {
    co_await commit_page(x, sink, pages.not_found.get(), 404,
                         k_not_found_body, nullptr);
}

// The method-miss branch: before_handler is the consultation point (a
// hook may supply the 405; Allow always rides), else the page. A
// contained before_handler throw surfaces through the
// handler_exception chain (D4) before the page synthesizes.
task<void> dispatch_method_miss(const server::hook_bus& bus,
                                const error_page_factories& pages,
                                const resolve_result& resolved,
                                lifecycle_sink& sink, exchange& x,
                                dispatch_state& st) {
    const std::string allow = render_allow(resolved);
    std::exception_ptr contained;
    if (bus.any_hooks(server::hook_phase::before_handler)) {
        server::before_handler_ctx ctx{x.head(), describe(resolved)};
        server::hook_action action =
            bus.fire<server::hook_phase::before_handler>(ctx, contained);
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
    if (contained
            && co_await consult_exception_chain(bus, sink, x, &allow, st,
                                                std::move(contained))) {
        co_return;
    }
    co_await commit_page(x, sink, pages.method_not_allowed.get(), 405,
                         k_method_not_allowed_body, &allow);
}

// The hit branch: before_handler may short-circuit, a contained
// before_handler throw surfaces (D4), else the handler runs with its
// captures stamped as path args.
task<void> dispatch_hit(const server::hook_bus& bus,
                        resolve_result& resolved, lifecycle_sink& sink,
                        exchange& x, dispatch_state& st) {
    std::exception_ptr contained;
    if (bus.any_hooks(server::hook_phase::before_handler)) {
        server::before_handler_ctx ctx{x.head(), describe(resolved)};
        server::hook_action action =
            bus.fire<server::hook_phase::before_handler>(ctx, contained);
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
    if (contained
            && co_await consult_exception_chain(bus, sink, x, nullptr, st,
                                                std::move(contained))) {
        co_return;
    }
    x.set_path_args(std::move(resolved.captures));
    co_await run_handler(bus, sink, st, *resolved.handler, x);
}

// request_received: the first short-circuit point. True when the hook
// supplied a response (an invalid status is treated as a pass); a
// contained throw surfaces through the handler_exception chain (D4)
// only when the phase did not answer.
task<bool> short_circuit_request_received(const server::hook_bus& bus,
                                          lifecycle_sink& sink, exchange& x,
                                          dispatch_state& st) {
    if (!bus.any_hooks(server::hook_phase::request_received)) {
        co_return false;
    }
    server::request_received_ctx ctx{x.head()};
    std::exception_ptr contained;
    server::hook_action action =
        bus.fire<server::hook_phase::request_received>(ctx, contained);
    if (!action.is_pass()) {
        server::hook_response page = std::move(action).take_response();
        if (page.status.valid()) {
            co_await commit_value(x, sink,
                                  lifecycle_sink::provenance::pre_handler,
                                  as_value(std::move(page)));
            co_return true;
        }
    }
    if (contained
            && co_await consult_exception_chain(bus, sink, x, nullptr, st,
                                                std::move(contained))) {
        co_return true;
    }
    co_return false;
}

// route_resolved on a hit and a miss alike (observation only); a
// contained throw surfaces through the handler_exception chain (D4),
// which may answer the exchange.
task<bool> fire_route_resolved(const server::hook_bus& bus, exchange& x,
                              const resolve_result& resolved,
                              lifecycle_sink& sink, dispatch_state& st) {
    if (!bus.any_hooks(server::hook_phase::route_resolved)) co_return false;
    server::route_resolved_ctx ctx{
        x.head(), resolved.kind == resolve_kind::hit, describe(resolved)};
    std::exception_ptr contained;
    // Observation-only: the action is ignored by contract.
    (void)bus.fire<server::hook_phase::route_resolved>(ctx, contained);
    if (!contained) co_return false;
    co_return co_await consult_exception_chain(bus, sink, x, nullptr, st,
                                               std::move(contained));
}

// The exactly-once tail. succeeded follows the v2 mapping: true when a
// complete response (or upgrade) reached the engine and nothing forced
// the exchange down afterwards; false with the typed end reason
// otherwise.
// The typed end reason of a failed settle: the exchange's disconnect
// reason when one is known, the engine's head refusal when the
// committed head was refused (an upgraded exchange is a success
// settle, never a refusal), else a generic failure.
http::outcome end_reason_of(const exchange& x, const lifecycle_sink& sink) {
    if (x.disconnected()) return x.disconnect_reason();
    if (x.state() == exchange_state::responded && !sink.responded()) {
        return http::outcome(
            http::outcome_code::invalid_argument,
            "request_lifecycle: the engine refused the committed"
            " response head");
    }
    return http::outcome(http::outcome_code::invalid_state,
                         "request_lifecycle: the exchange ended without a"
                         " terminal decision");
}

void fire_completed(const server::hook_bus& bus, const exchange& x,
                    const lifecycle_sink& sink) {
    if (!bus.any_hooks(server::hook_phase::request_completed)) return;
    const bool settled_clean = x.terminal() && !x.disconnected()
        && !sink.aborted() && !sink.refused()
        && (sink.responded()
            || x.state() == exchange_state::upgraded);
    server::request_completed_ctx ctx{
        x.head(), settled_clean,
        settled_clean ? http::outcome::okay() : end_reason_of(x, sink)};
    (void)bus.fire<server::hook_phase::request_completed>(ctx);
}

}  // namespace

task<void> dispatch_request(const server::route_registry& routes,
                            const server::hook_bus& bus,
                            const error_page_factories& pages,
                            lifecycle_sink& sink, exchange& x) {
    dispatch_state st;
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
    if (co_await short_circuit_request_received(bus, sink, x, st)) {
        fire_completed(bus, x, sink);
        co_return;
    }
    // The full resolve tier merges every matching entry's methods past
    // its first hit only when a hook can observe them (the descriptor
    // of route_resolved/before_handler contexts); otherwise it stops
    // at the hit (routes.hpp documents the rule).
    const bool methods_observable =
        bus.any_hooks(server::hook_phase::route_resolved)
        || bus.any_hooks(server::hook_phase::before_handler);
    resolve_result resolved = routes.resolve(
        x.head().request_method, x.head().route_path, methods_observable);
    if (co_await fire_route_resolved(bus, x, resolved, sink, st)) {
        fire_completed(bus, x, sink);
        co_return;
    }
    switch (resolved.kind) {
        case resolve_kind::miss:
            co_await dispatch_miss(pages, sink, x);
            break;
        case resolve_kind::method_miss:
            co_await dispatch_method_miss(bus, pages, resolved, sink, x, st);
            break;
        case resolve_kind::hit:
            co_await dispatch_hit(bus, resolved, sink, x, st);
            break;
    }
    fire_completed(bus, x, sink);
}

}  // namespace detail

}  // namespace httpserver
