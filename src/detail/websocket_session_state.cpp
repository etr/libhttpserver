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

#include <httpserver/detail/websocket_session_state.hpp>
#include <string>
#include <deque>
#include <utility>
namespace httpserver::websocket::detail {
namespace {
close_info decode_close(std::span<const std::byte> payload) {
    close_info info;
    if (!payload.empty()) {
        info.code = (std::to_integer<unsigned>(payload[0]) << 8) | std::to_integer<unsigned>(payload[1]);
        info.reason.assign(reinterpret_cast<const char*>(payload.data() + 2), payload.size() - 2);
    }
    return info;
}
}
wire_frame::wire_frame(unsigned opcode, std::span<const std::byte> payload) {
    auto encoded = httpserver::detail::websocket_encode(opcode, payload);
    bytes.assign(encoded.begin(), encoded.end());
    control = opcode >= 8; close = opcode == 8;
}
bool session_state::writable_ready() const {
    return closing || done || (outgoing_messages < limits.outgoing_messages && limits.output_bytes - output_bytes >= 2);
}
bool session_state::receive_ready() const { return codec.has_message() || done || peer_close; }
close_info session_state::best_reason() const { return peer_close ? peer_info : local_info; }
session_notifications session_state::notifications() {
    session_notifications out;
    if (receive_ready()) out.receive = std::exchange(receive_wait, {});
    if (writable_ready()) out.writable = std::exchange(writable_wait, {});
    if (done && on_close) {
        out.callback = std::move(on_close); out.info = final_info;
    }
    return out;
}
void session_state::terminal(http::outcome reason, bool clean) {
    if (done) return;
    done = true; closing = true;
    final_info = best_reason(); final_info.status = std::move(reason); final_info.clean = clean;
    codec.clear(); std::deque<wire_frame>().swap(data);
    active.reset(); pong.reset(); close_frame.reset();
    output_bytes = 0; outgoing_messages = 0; offered = 0;
}
void session_state::handshake_complete() {
    if (peer_close && close_sent) terminal(http::outcome::okay(), true);
}
void session_state::begin_close(std::span<const std::byte> payload) {
    closing = true;
    // Already offered output stays pinned; discard only unoffered frames.
    for (const auto& frame : data) {
        output_bytes -= frame.bytes.size(); --outgoing_messages;
    }
    std::deque<wire_frame>().swap(data); pong.reset();
    close_frame.emplace(8, payload);
}
void session_state::control(httpserver::detail::websocket_control frame) {
    if (frame.opcode == 9) {
        if (!peer_close) pong.emplace(10, frame.payload);  // bounded latest reply
        return;
    }
    if (frame.opcode != 8 || peer_close) return;
    peer_info = decode_close(frame.payload); peer_close = true;
    if (!closing) begin_close(frame.payload);
    handshake_complete();
}
bool session_state::select_output() {
    if (active) return true;
    if (close_frame) {
        active = std::move(close_frame); close_frame.reset();
    } else if (pong) {
        active = std::move(pong); pong.reset();
    } else if (!data.empty()) {
        active = std::move(data.front()); data.pop_front();
    }
    return active.has_value();
}
void session_wait_node::complete() noexcept {
    if (claimed.exchange(true, std::memory_order_acq_rel)) return;
    auto resume = [witness = witness, frame = frame] {
        if (witness) httpserver::detail::guarded_resume(witness, frame);
        else frame.resume();
    };
    try {
        if (target) {
            target->post(std::move(resume));
        } else {
            resume();
        }
    } catch (...) { }
}
void session_notifications::deliver() noexcept {
    if (receive) receive->complete();
    if (writable) writable->complete();
    try {
        if (callback) (*callback)(std::move(info));
    } catch (...) { }
}
session_operation::session_operation(std::shared_ptr<session_state> state, bool receive)
    : state_(std::move(state)), receive_(receive), admitted_(false) {
    if (!state_) return;
    std::lock_guard lock(state_->mu);
    bool& pending = receive_ ? state_->receive_pending : state_->writable_pending;
    admitted_ = !pending;
    if (admitted_) pending = true;
}
session_operation::~session_operation() {
    if (!admitted_) return;
    std::lock_guard lock(state_->mu);
    (receive_ ? state_->receive_pending : state_->writable_pending) = false;
}
session_wait::~session_wait() {
    std::lock_guard lock(state_->mu);
    auto& slot = receive_ ? state_->receive_wait : state_->writable_wait;
    if (slot == node_) slot.reset();
    node_->claimed.store(true, std::memory_order_release);
}
void session_wait::await_suspend(std::coroutine_handle<> frame) {
    node_->frame = frame;
    node_->witness = frame_ ? frame_->frame_witness_ptr() : nullptr;
    node_->target = frame_ ? frame_->frame_executor() : current_executor();
    bool ready;
    {
        std::lock_guard lock(state_->mu);
        ready = receive_ ? state_->receive_ready() : state_->writable_ready();
        if (!ready) (receive_ ? state_->receive_wait : state_->writable_wait) = node_;
    }
    if (ready) node_->complete();
}
task<receive_result> receive_session(std::shared_ptr<session_state> state) {
    if (!state) co_return receive_result{{http::outcome_code::connection_closed, "empty session"}, {}};
    session_operation lease(state, true);
    if (!lease.admitted()) co_return receive_result{{http::outcome_code::invalid_state, "receive already outstanding"}, {}};
    for (;;) {
        {
            std::lock_guard lock(state->mu);
            if (auto message = state->codec.pop()) co_return receive_result{http::outcome::okay(), std::move(message)};
            if (state->done) {
                auto reason = state->final_info.status;
                if (reason.ok()) reason = {http::outcome_code::connection_closed, "Close handshake complete"};
                co_return receive_result{std::move(reason), {}};
            }
            if (state->peer_close) co_return receive_result{{http::outcome_code::connection_closed, "peer Close received"}, {}};
        }
        session_wait wait(state, true); co_await wait;
    }
}
task<http::outcome> writable_session(std::shared_ptr<session_state> state) {
    if (!state) co_return http::outcome{http::outcome_code::connection_closed, "empty session"};
    session_operation lease(state, false);
    if (!lease.admitted()) co_return http::outcome{http::outcome_code::invalid_state, "writable already outstanding"};
    for (;;) {
        {
            std::lock_guard lock(state->mu);
            if (state->done && !state->final_info.status.ok()) co_return state->final_info.status;
            if (state->closing) co_return http::outcome{http::outcome_code::connection_closed, "session closing"};
            if (state->writable_ready()) co_return http::outcome::okay();
        }
        session_wait wait(state, false); co_await wait;
    }
}
}  // namespace httpserver::websocket::detail
