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

#ifndef SRC_HTTPSERVER_DETAIL_WEBSOCKET_SESSION_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_WEBSOCKET_SESSION_STATE_HPP_
#include <atomic>
#include <deque>
#include <mutex>
#include <memory>
#include <optional>
#include <utility>
#include <httpserver/detail/websocket_codec.hpp>
#include <httpserver/websocket/session.hpp>
namespace httpserver::websocket::detail {
struct session_wait_node {
    std::atomic<bool> claimed{false};
    std::coroutine_handle<> frame;
    std::shared_ptr<httpserver::detail::frame_witness> witness;
    executor* target = nullptr;
    void complete() noexcept;
};
struct session_notifications {
    std::shared_ptr<session_wait_node> receive, writable;
    std::unique_ptr<session::close_callback> callback;
    close_info info;
    void deliver() noexcept;
};
struct wire_frame {
    // A deque releases consumed storage without retaining a vector prefix.
    // Frame counts independently cap per-container allocation overhead.
    std::deque<std::byte> bytes;
    bool control = false, close = false;
    wire_frame() = default;
    wire_frame(unsigned opcode, std::span<const std::byte> payload);
};
struct session_state {
    session_state(options limits, session::close_callback callback)
        : limits(limits), codec(limits), callback_registered(static_cast<bool>(callback)), on_close(callback ? std::make_unique<session::close_callback>(std::move(callback)) : nullptr) { }
    bool input_stopped() const { return done || peer_close; }
    bool writable_ready() const;
    bool receive_ready() const;
    session_notifications notifications();  // caller holds mu
    void terminal(http::outcome reason, bool clean = false);  // caller holds mu
    void begin_close(std::span<const std::byte> payload);  // caller holds mu
    void control(httpserver::detail::websocket_control frame);
    bool select_output();
    void handshake_complete();
    close_info best_reason() const;

    std::mutex mu;
    options limits;
    httpserver::detail::websocket_codec codec;
    std::deque<wire_frame> data;
    std::optional<wire_frame> active, pong, close_frame;
    std::size_t output_bytes = 0, outgoing_messages = 0, offered = 0;
    bool closing = false, done = false, peer_close = false, close_sent = false;
    bool receive_pending = false, writable_pending = false;
    std::shared_ptr<session_wait_node> receive_wait, writable_wait;
    bool callback_registered = false;
    std::unique_ptr<session::close_callback> on_close;
    close_info final_info, peer_info, local_info;
};
// Keeps the single-operation lease alive until completion or frame teardown.
// Registration and condition checks share session_state::mu with producers.
class session_operation {
 public:
    session_operation(std::shared_ptr<session_state> state, bool receive);
    ~session_operation();
    bool admitted() const noexcept { return admitted_; }
 private:
    std::shared_ptr<session_state> state_;
    bool receive_, admitted_;
};
class session_wait {
 public:
    session_wait(std::shared_ptr<session_state> state, bool receive)
        : state_(std::move(state)), receive_(receive), node_(std::make_shared<session_wait_node>()) { }
    ~session_wait();
    session_wait& bind_frame(httpserver::detail::task_frame_base* frame) { frame_ = frame; return *this; }
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> frame);
    void await_resume() const noexcept { }
 private:
    std::shared_ptr<session_state> state_;
    bool receive_;
    std::shared_ptr<session_wait_node> node_;
    httpserver::detail::task_frame_base* frame_ = nullptr;
};
task<receive_result> receive_session(std::shared_ptr<session_state> state);
task<http::outcome> writable_session(std::shared_ptr<session_state> state);
}  // namespace httpserver::websocket::detail
#endif  // SRC_HTTPSERVER_DETAIL_WEBSOCKET_SESSION_STATE_HPP_
