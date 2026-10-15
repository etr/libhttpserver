/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/exchange_runner.hpp>
#include <utility>

namespace httpserver::detail {

auto recording_sink::on_admit(const body_policy& policy) -> void {
    ++admit_calls;
    admitted_bytes = policy.max_buffer_bytes;
}

auto recording_sink::on_respond(const http::status& s, const http::fields& f) -> void {
    ++respond_calls;
    respond_code = s.code();
    respond_fields_size = f.size();
}

auto recording_sink::on_upgrade(const ws_upgrade_options& options) -> websocket_upgrade_result {
    ++upgrade_calls;
    upgrade_subprotocols = options.subprotocols.size();
    websocket_upgrade_result result;
    result.session.emplace(options.limits);
    return result;
}

void commit_error(exchange& x, std::uint16_t code) {
    if (x.disconnected()) return;
    static_cast<void>(x.respond(http::status::from_code(code), http::fields()));
}

task<void> invoke_route_handler(const server::route_handler& handler, exchange& x) {
    task<void> handler_task = handler(x);
    co_await std::move(handler_task);
}

task<void> run_route(const server::route_registry& routes, exchange& x) {
    // Defensive: a head without a valid method never matches a
    // registration; answer 501 instead of reporting a miss.
    if (!x.head().request_method.valid()) {
        commit_error(x, 501);
        co_return;
    }
    const server::route_registry::match_result found = routes.match(x.head().request_method, x.head().route_path);
    if (found.handler == nullptr) {
        commit_error(x, 404);
        co_return;
    }
    try {
        co_await invoke_route_handler(*found.handler, x);
    } catch (const cancelled_exception&) {
        // Disconnect cancellation is a quiet end, not an error: the
        // engine already owns the connection's fate.
    } catch (...) {
        if (x.terminal()) {
            // The response was already committed; the engine resets or
            // closes instead of committing a second response.
            static_cast<void>(x.abort());
        } else {
            commit_error(x, 500);
        }
        co_return;
    }
    if (!x.terminal()) commit_error(x, 500);
}

}  // namespace httpserver::detail
