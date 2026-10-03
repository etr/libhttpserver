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
#include <algorithm>
#include <memory>
#include <vector>
#include <stdexcept>
#include <utility>
namespace httpserver::websocket {
namespace {
http::outcome closed() { return {http::outcome_code::connection_closed, "session closed"}; }
std::span<const std::byte> reason_bytes(std::string_view reason) {
    return {reinterpret_cast<const std::byte*>(reason.data()), reason.size()};
}
http::outcome validate_message(message_kind kind, std::span<const std::byte> payload, std::size_t cap) {
    if (kind != message_kind::text && kind != message_kind::binary)
        return {http::outcome_code::invalid_argument, "invalid message kind"};
    if (payload.size() > cap)
        return {http::outcome_code::limit_exceeded, "outgoing message limit exceeded"};
    if (kind == message_kind::text && !httpserver::detail::websocket_utf8::valid(payload))
        return {http::outcome_code::invalid_argument, "invalid text UTF-8"};
    return http::outcome::okay();
}
}  // namespace
session::session(options limits, close_callback callback) {
    auto valid = limits.validate();
    if (!valid.ok()) throw std::invalid_argument(valid.message());
    state_ = std::make_shared<detail::session_state>(limits, std::move(callback));
}
session::~session() { cancel(); }
session::session(session&& other) noexcept : state_(std::move(other.state_)) { }
session& session::operator=(session&& other) noexcept {
    if (this != &other) {
        cancel(); state_ = std::move(other.state_);
    }
    return *this;
}
send_result session::try_send(message_kind kind, std::span<const std::byte> payload) {
    if (!state_) return {closed(), send_disposition::closed};
    detail::session_notifications notify;
    {
        std::lock_guard lock(state_->mu);
        if (state_->closing) return {closed(), send_disposition::closed};
        auto valid = validate_message(kind, payload, state_->limits.max_message_bytes);
        if (!valid.ok()) return {std::move(valid), send_disposition::rejected};
        const std::size_t overhead = payload.size() < 126 ? 2 : payload.size() <= 65535 ? 4 : 10;
        const std::size_t bytes = payload.size() + overhead;
        if (state_->outgoing_messages == state_->limits.outgoing_messages || bytes > state_->limits.output_bytes - state_->output_bytes)
            return {http::outcome::okay(), send_disposition::backpressured};
        state_->data.emplace_back(static_cast<unsigned>(kind), payload);
        state_->output_bytes += bytes; ++state_->outgoing_messages;
        notify = state_->notifications();
    }
    notify.deliver();
    return {http::outcome::okay(), send_disposition::accepted};
}
task<receive_result> session::receive() { return detail::receive_session(state_); }
task<http::outcome> session::writable() { return detail::writable_session(state_); }
http::outcome session::close(std::uint16_t code, std::string_view reason) {
    if (!httpserver::detail::websocket_close_code(code) || reason.size() > 123 || !httpserver::detail::websocket_utf8::valid(reason_bytes(reason)))
        return {http::outcome_code::invalid_argument, "invalid local Close payload"};
    if (!state_) return closed();
    detail::session_notifications notify;
    {
        std::lock_guard lock(state_->mu);
        if (state_->closing) return {http::outcome_code::invalid_state, "Close already started"};
        std::vector<std::byte> payload{std::byte(code >> 8), std::byte(code)};
        auto bytes = reason_bytes(reason); payload.insert(payload.end(), bytes.begin(), bytes.end());
        state_->local_info.code = code; state_->local_info.reason = reason;
        state_->begin_close(payload); notify = state_->notifications();
    }
    notify.deliver(); return http::outcome::okay();
}
http::outcome session::on_close(close_callback callback) {
    if (!callback) return {http::outcome_code::invalid_argument, "empty close callback"};
    if (!state_) return closed();
    std::lock_guard lock(state_->mu);
    if (state_->done || state_->callback_registered)
        return {http::outcome_code::invalid_state, "close callback already registered or session closed"};
    state_->on_close = std::make_unique<close_callback>(std::move(callback));
    state_->callback_registered = true;
    return http::outcome::okay();
}
void session::transport_failed(http::outcome reason) {
    if (!state_) return;
    if (reason.ok()) reason = closed();
    detail::session_notifications notify;
    { std::lock_guard lock(state_->mu); state_->terminal(std::move(reason)); notify = state_->notifications(); }
    notify.deliver();
}
void session::cancel(http::outcome reason) {
    if (reason.ok()) reason = {http::outcome_code::cancelled, "session cancelled"};
    transport_failed(std::move(reason));
}
}  // namespace httpserver::websocket
