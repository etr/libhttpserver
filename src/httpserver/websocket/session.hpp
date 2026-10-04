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

#ifndef SRC_HTTPSERVER_WEBSOCKET_SESSION_HPP_
#define SRC_HTTPSERVER_WEBSOCKET_SESSION_HPP_
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <httpserver/concurrency/task.hpp>
#include <httpserver/websocket/message.hpp>
#include <httpserver/websocket/options.hpp>
namespace httpserver::detail { class websocket_driver; }
namespace httpserver::websocket {
namespace detail { struct session_state; }
// Transport-neutral server session. All entry points serialize shared
// state; handle moves/destruction must not race access to that handle.
// Lazy tasks capture shared state before starting and resume on their
// executor. One receive and one writable operation may be outstanding.
// Returned messages own their data. Protocol adapters own the byte-stream
// driver separately from this application handle.
class session {
 public:
    using close_callback = std::function<void(close_info)>;
    // Throws invalid_argument for an options object failing validate().
    explicit session(options limits = {}, close_callback on_close = {});
    ~session();
    session(session&&) noexcept;
    session& operator=(session&&) noexcept;
    session(const session&) = delete;
    session& operator=(const session&) = delete;

    // Copies the entire payload on accepted. All other dispositions leave
    // queues unchanged. rejected distinguishes invalid/oversize arguments
    // from temporary backpressure, which carries an ok status.
    send_result try_send(message_kind kind, std::span<const std::byte> payload);
    task<receive_result> receive();
    // Capacity for at least an empty frame became available. Recheck
    // try_send: this neither acknowledges delivery nor promises arbitrary
    // message admission. Closing wakes the wait with connection_closed.
    task<http::outcome> writable();
    // Retain the handle through on_close; Close waits for a peer reply
    // within the server ws_close budget, also bounded by any server drain.
    http::outcome close(std::uint16_t code = 1000, std::string_view reason = {});

    // Register once, before terminal closure. Constructor callbacks occupy
    // the same registration slot. Empty callbacks are invalid arguments.
    http::outcome on_close(close_callback callback);

 private:
    friend class httpserver::detail::websocket_driver;
    explicit session(std::shared_ptr<detail::session_state> state) : state_(std::move(state)) { }
    void cancel(http::outcome reason = {http::outcome_code::cancelled, "session cancelled"});
    void transport_failed(http::outcome reason);
    std::shared_ptr<detail::session_state> state_;
};
}  // namespace httpserver::websocket
#endif  // SRC_HTTPSERVER_WEBSOCKET_SESSION_HPP_
