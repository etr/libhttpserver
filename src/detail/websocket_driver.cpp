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
#include <httpserver/detail/websocket_driver.hpp>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>
namespace httpserver::detail {
namespace {
http::outcome closed() { return {http::outcome_code::connection_closed, "session closed"}; }
}
websocket_driver::websocket_driver(websocket::options limits, websocket::session::close_callback callback) {
    auto valid = limits.validate();
    if (!valid.ok()) throw std::invalid_argument(valid.message());
    state_ = std::make_shared<websocket::detail::session_state>(limits, std::move(callback));
}
websocket_driver::~websocket_driver() { cancel(); }
websocket::session websocket_driver::take_session() {
    if (session_taken_) throw std::logic_error("session already taken");
    session_taken_ = true;
    return websocket::session(state_);
}
websocket::feed_result websocket_driver::feed(std::span<const std::byte> input) {
    if (!state_) return {closed(), 0, false};
    websocket::feed_result out;
    websocket::detail::session_notifications notify;
    {
        std::lock_guard lock(state_->mu);
        if (state_->input_stopped()) return {closed(), 0, false};
        for (;;) {
            auto part = state_->codec.feed(input.subspan(out.consumed));
            out.consumed += part.consumed; out.status = part.status; out.blocked = part.blocked;
            if (!part.status.ok()) {
                state_->terminal(part.status);
                break;
            }
            if (auto control = state_->codec.take_control()) state_->control(std::move(*control));
            if (state_->input_stopped() || part.blocked || out.consumed == input.size()) break;
        }
        notify = state_->notifications();
    }
    notify.deliver(); return out;
}
std::size_t websocket_driver::copy_output(std::span<std::byte> into) {
    if (!state_ || into.empty()) return 0;
    std::lock_guard lock(state_->mu);
    if (!state_->select_output()) return 0;
    const std::size_t count = std::min(into.size(), state_->active->bytes.size());
    std::copy_n(state_->active->bytes.begin(), count, into.begin());
    state_->offered = std::max(state_->offered, count); return count;
}
http::outcome websocket_driver::consume_output(std::size_t count) {
    if (!state_) return closed();
    websocket::detail::session_notifications notify;
    {
        std::lock_guard lock(state_->mu);
        if (count > state_->offered) return {http::outcome_code::invalid_argument, "consumption exceeds offered output"};
        if (count == 0) return http::outcome::okay();
        auto& frame = *state_->active;
        for (std::size_t i = 0; i < count; ++i) frame.bytes.pop_front();
        state_->offered -= count;
        if (!frame.control) state_->output_bytes -= count;
        if (frame.bytes.empty()) {
            if (frame.close) state_->close_sent = true;
            if (!frame.control) --state_->outgoing_messages;
            state_->active.reset(); state_->handshake_complete();
        }
        notify = state_->notifications();
    }
    notify.deliver(); return http::outcome::okay();
}
websocket::queue_usage websocket_driver::usage() const {
    if (!state_) return {};
    std::lock_guard lock(state_->mu);
    return {state_->codec.incoming_bytes(), state_->codec.incoming_messages(), state_->output_bytes, state_->outgoing_messages};
}
void websocket_driver::observe_progress(std::function<void()> observer) {
    auto owned = observer ? std::make_shared<const std::function<void()>>(std::move(observer)) : nullptr;
    std::lock_guard lock(state_->mu);
    state_->progress = std::move(owned);
}
websocket_progress websocket_driver::snapshot() const {
    std::lock_guard lock(state_->mu);
    return {!state_->input_stopped() && state_->codec.input_ready(),
        state_->output_pending(), state_->closing, state_->done, state_->output_pending_since};
}
void websocket_driver::eof() { transport_failed({http::outcome_code::connection_closed, "transport EOF before Close handshake"}); }
void websocket_driver::transport_failed(http::outcome reason) {
    if (reason.ok()) reason = closed();
    websocket::detail::session_notifications notify;
    { std::lock_guard lock(state_->mu); state_->terminal(std::move(reason)); notify = state_->notifications(); }
    notify.deliver();
}
void websocket_driver::cancel(http::outcome reason) {
    if (reason.ok()) reason = {http::outcome_code::cancelled, "driver cancelled"};
    transport_failed(std::move(reason));
}
}  // namespace httpserver::detail
