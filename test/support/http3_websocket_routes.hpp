/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_SUPPORT_HTTP3_WEBSOCKET_ROUTES_HPP_
#define TEST_SUPPORT_HTTP3_WEBSOCKET_ROUTES_HPP_
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/server/routes.hpp>
namespace h3wstest {
namespace h = httpserver;
struct observation {
    std::size_t received = 0, received_bytes = 0, accepted = 0, blocked = 0, resumed = 0, close_count = 0;
    unsigned kind = 0;
    bool waiting = false, clean = false, retired = false;
    std::optional<std::uint16_t> code;
    std::string reason, subprotocol;
    h::resume_signal closed;
    std::function<void(const char*)> emit = [](const char*) {};
};
using enter = std::function<std::shared_ptr<observation>(h::exchange&)>;
inline const char* close_reason(h::http::outcome_code code) {
    switch (code) {
        case h::http::outcome_code::ok: return "ok";
        case h::http::outcome_code::cancelled: return "cancelled";
        case h::http::outcome_code::protocol_error: return "protocol_error";
        case h::http::outcome_code::timeout: return "timeout";
        default: return "connection_closed";
    }
}
struct retirement {
    std::shared_ptr<observation> state;
    ~retirement() { state->retired = true; state->emit("ws_retired"); }
};
inline h::task<bool> send(h::websocket::session& session, h::websocket::message_kind kind,
                         std::span<const std::byte> bytes, std::shared_ptr<observation> state) {
    for (;;) {
        auto result = session.try_send(kind, bytes);
        if (result.disposition == h::websocket::send_disposition::accepted) {
            ++state->accepted; state->emit("ws_sent"); co_return true;
        }
        if (result.disposition == h::websocket::send_disposition::closed) co_return false;
        if (result.disposition != h::websocket::send_disposition::backpressured || !result.status.ok())
            throw std::runtime_error("WebSocket fixture send rejected");
        ++state->blocked; state->waiting = true; state->emit("ws_blocked");
        auto writable = co_await session.writable();
        state->waiting = false;
        if (!writable.ok()) co_return false;
        ++state->resumed; state->emit("ws_resumed");
    }
}
inline h::task<void> route(h::exchange& x, enter entered, bool flood) {
    auto state = entered(x);
    retirement guard{state};
    h::ws_upgrade_options options;
    options.subprotocols = {"harness.v1"}; options.require_subprotocol = true;
    options.limits = {8192, 8192, 8202, 1, 1};
    // The server's public upgrade API refuses all unsupported extensions.
    auto result = co_await x.upgrade(options);
    if (!result.status.ok()) {
        state->emit("ws_refused");
        x.respond(result.rejection_status, result.rejection_fields); co_return;
    }
    state->subprotocol = result.selected_subprotocol;
    auto session = std::move(*result.session);
    if (!session.on_close([state](h::websocket::close_info info) {
        ++state->close_count; state->reason = close_reason(info.status.code());
        state->clean = info.clean; state->code = info.code;
        state->emit("ws_closed"); state->closed.signal();
    }).ok()) throw std::runtime_error("WebSocket close observer refused");
    state->emit("ws_accepted");
    if (flood) {
        std::array<std::byte, 8192> payload{};
        for (unsigned index = 0; index < 64; ++index) {
            payload.fill(static_cast<std::byte>(index));
            if (!(co_await send(session, h::websocket::message_kind::binary, payload, state))) co_return;
        }
        // close() may discard queued application frames. With a one-message
        // queue, writable() establishes that the last frame left the codec.
        state->waiting = true; state->emit("ws_flushing");
        auto flushed = co_await session.writable();
        state->waiting = false; state->emit("ws_flushed");
        if (!flushed.ok()) co_return;
        if (!session.close(1000).ok()) throw std::runtime_error("WebSocket close refused");
    } else {
        for (;;) {
            auto message = co_await session.receive();
            if (!message.value) break;
            if (!message.status.ok() || ++state->received > 64) throw std::runtime_error("WebSocket input bound");
            state->received_bytes += message.value->data.size();
            state->kind = static_cast<unsigned>(message.value->kind); state->emit("ws_received");
            if (!(co_await send(session, message.value->kind, message.value->data, state))) {
                break;
            }
        }
    }
    if (!state->close_count) co_await state->closed.wait();
}
inline void install(h::server::route_registry& routes, enter entered) {
    auto method = h::http::method::known(h::http::method_id::connect);
    auto echo = routes.route(method, "/ws/echo", [entered](h::exchange& x) { return route(x, entered, false); });
    auto flood = routes.route(method, "/ws/flood", [entered](h::exchange& x) { return route(x, entered, true); });
    if (!echo.ok() || !flood.ok())
        throw std::runtime_error("WebSocket fixture route refused");
}
}  // namespace h3wstest
#endif  // TEST_SUPPORT_HTTP3_WEBSOCKET_ROUTES_HPP_
