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

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <httpserver/server/server.hpp>
#include <httpserver/exchange.hpp>

#include <httpserver/concurrency/resume_signal.hpp>
namespace h = httpserver;
namespace {
std::mutex log_mu;
std::atomic<int> accepted{0}, closed{0};
h::resume_signal send_gate;
void event(const std::string& text) { std::lock_guard lock(log_mu); std::cout << text << std::endl; }
h::task<void> route(h::exchange& x) {
    h::ws_upgrade_options options;
    options.subprotocols = {"chat", "binary"};
    options.allowed_origins = {"https://example.com"};
    options.allow_absent_origin = true;
    options.limits.max_message_bytes = 1048576;
    options.limits.incoming_bytes = 1048576;
    options.limits.incoming_messages = 1;
    options.limits.output_bytes = 1048586;
    options.limits.outgoing_messages = 1;
    auto result = co_await x.upgrade(options);
    if (!result.status.ok()) {
        result.rejection_fields.append("Content-Length", "0");
        result.rejection_fields.append("Connection", "close");
        x.respond(result.rejection_status, result.rejection_fields);
        event("REFUSED " + std::to_string(result.rejection_status.code()));
        co_return;
    }
    ++accepted; event("ACCEPT " + result.selected_subprotocol);
    auto session = std::move(*result.session);
    h::resume_signal close_gate;
    session.on_close([close_gate](auto info) mutable { ++closed; close_gate.signal(); event("CLOSE " + std::to_string(info.clean) + " " + std::to_string(static_cast<int>(info.status.code()))); });
    if (x.head().route_path == "/return") co_return;
    if (x.head().route_path == "/throw") throw std::runtime_error("upgraded handler failure");
    if (x.head().route_path == "/idle") {
        event("IDLE");
        co_await send_gate.wait_for(std::chrono::seconds(5));
        const std::string text = "unsolicited";
        session.try_send(h::websocket::message_kind::text,
            {reinterpret_cast<const std::byte*>(text.data()), text.size()});
    }
    if (x.head().route_path == "/flood") {
        std::vector<std::byte> payload(1048576);
        for (unsigned i = 0; i < 64; ++i) {
            std::fill(payload.begin(), payload.end(), std::byte(i));
            for (;;) {
                auto sent = session.try_send(h::websocket::message_kind::binary, payload);
                if (sent.disposition == h::websocket::send_disposition::accepted) break;
                if (sent.disposition == h::websocket::send_disposition::closed) {
                    event("SEND CLOSED"); co_return;
                }
                event("BLOCKED " + std::to_string(i));
                auto room = co_await session.writable();
                if (!room.ok()) {
                    event("WRITABLE CLOSED"); co_return;
                }
                event("RESUMED " + std::to_string(i));
            }
        }
        event("SENT 64");
    }
    for (;;) {
        auto message = co_await session.receive();
        if (!message.value) {
            // Retain the owning handle through the peer-close reply.
            // Application close callback is separate from transport wake.
            co_await close_gate.wait_for(std::chrono::seconds(5));
            co_return;
        }
        auto& incoming = *message.value;
        for (;;) {
            auto sent = session.try_send(incoming.kind, incoming.data);
            if (sent.disposition == h::websocket::send_disposition::accepted) break;
            if (sent.disposition != h::websocket::send_disposition::backpressured) co_return;
            if (!(co_await session.writable()).ok()) co_return;
        }
    }
}
}  // namespace
int main() {
    h::server::server_options options;
    h::server::listener_options listener; listener.address = "127.0.0.1";
    options.add_listener(listener); options.concurrency().workers = 2;
    options.timeouts().header = std::chrono::milliseconds(100);
    if (!options.validate().ok()) return 2;
    h::server::native_server server(std::move(options));
    h::http::method_set get; get.set(h::http::method_id::get);
    server.route_prefix(get, "/", route);
    if (!server.listen().ok()) return 2;
    event("READY " + std::to_string(server.get_bound_port(0)));
    std::string command;
    while (std::getline(std::cin, command)) {
        if (command == "STOP") break;
        if (command == "SEND") send_gate.signal();
        if (command == "STATS") event("STATS " + std::to_string(accepted.load()) + " " + std::to_string(closed.load()));
    }
    server.request_stop(); server.stop(); event("STOPPED");
}
