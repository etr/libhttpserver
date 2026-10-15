/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef EXAMPLES_V3_EXAMPLE_WS_HPP_
#define EXAMPLES_V3_EXAMPLE_WS_HPP_
#include <chrono>
#include <utility>

#include <httpserver/exchange.hpp>
namespace v3_example {
inline httpserver::task<void> echo_websocket(httpserver::exchange& x, httpserver::ws_upgrade_options options = {}) {
    auto upgraded = co_await x.upgrade(std::move(options));
    if (!upgraded.status.ok()) {
        if (upgraded.rejection_fields.count("Content-Length") == 0) {
            upgraded.rejection_fields.append("Content-Length", "0");
        }
        x.respond(upgraded.rejection_status, upgraded.rejection_fields);
        co_return;
    }
    auto session = std::move(*upgraded.session);
    httpserver::resume_signal closed;
    session.on_close([closed](httpserver::websocket::close_info) mutable { closed.signal(); });
    bool sending = true;
    while (sending) {
        const auto received = co_await session.receive();
        if (!received.status.ok() || !received.value) {
            break;
        }
        for (;;) {
            const auto sent = session.try_send(received.value->kind, received.value->data);
            if (sent.disposition == httpserver::websocket::send_disposition::accepted) {
                break;
            }
            if (sent.disposition != httpserver::websocket::send_disposition::backpressured ||
                !(co_await session.writable()).ok()) {
                sending = false;
                break;
            }
        }
    }
    // Every exit retains the owning session through a bounded close notification.
    co_await closed.wait_for(std::chrono::seconds(5));
}
}  // namespace v3_example
#endif  // EXAMPLES_V3_EXAMPLE_WS_HPP_
