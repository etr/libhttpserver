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

#include <utility>
#include <type_traits>
#include <httpserver/websocket/message.hpp>
#include <httpserver/exchange.hpp>
#include <httpserver/concurrency/resume_signal.hpp>
#include <httpserver/websocket/options.hpp>
#include <httpserver/websocket/session.hpp>
static_assert(!std::is_copy_constructible_v<httpserver::websocket::session>);
static_assert(std::is_move_constructible_v<httpserver::websocket::session>);
namespace httpserver {
task<void> native_echo(exchange& x) {
    ws_upgrade_options options;
    options.subprotocols = {"chat"};
    options.allowed_origins = {"https://example.com"};
    options.allow_absent_origin = false;
    auto upgraded = co_await x.upgrade(std::move(options));
    if (!upgraded.status.ok()) {
        upgraded.rejection_fields.append("Content-Length", "0");
        x.respond(upgraded.rejection_status, upgraded.rejection_fields);
        co_return;
    }
    auto session = std::move(*upgraded.session);
    resume_signal closed;
    session.on_close([closed](websocket::close_info) mutable {
        closed.signal();
    });
    for (;;) {
        auto received = co_await session.receive();
        if (!received.value) break;
        for (;;) {
            auto sent = session.try_send(received.value->kind,
                                         received.value->data);
            if (sent.disposition == websocket::send_disposition::accepted)
                break;
            if (sent.disposition != websocket::send_disposition::backpressured)
                co_return;
            if (!(co_await session.writable()).ok()) co_return;
        }
    }
    co_await closed.wait_for(std::chrono::seconds(5));
}
}  // namespace httpserver
int main() {
    httpserver::websocket::session s;
    return s.try_send(httpserver::websocket::message_kind::text, {}).status.ok() ? 0 : 1;
}
