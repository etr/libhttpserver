/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <httpserver/detail/quic_connection_lifecycle.hpp>
namespace httpserver::detail {
namespace {
using duration = quic_connection_lifecycle::duration;
using time_point = quic_connection_lifecycle::time_point;
duration three_pto(duration pto) {
    pto = std::max(pto, duration::zero());
    return pto > duration::max() / 3 ? duration::max() : pto * 3;
}
time_point after(time_point now, duration delay) {
    return now > time_point::max() - delay ? time_point::max() : now + delay;
}
}  // namespace
quic_connection_lifecycle::quic_connection_lifecycle(std::chrono::milliseconds local, std::chrono::milliseconds peer, time_point now) : activity_(now) {
    negotiate(local, peer);
}
void quic_connection_lifecycle::negotiate(std::chrono::milliseconds local, std::chrono::milliseconds peer) {
    if (state_ != quic_lifecycle_state::active) return;
    auto negotiated = local.count() > 0 && peer.count() > 0 ? std::min(local, peer) : std::max(local, peer);
    auto maximum = std::chrono::duration_cast<std::chrono::milliseconds>(duration::max());
    idle_ = negotiated >= maximum ? duration::max() : std::chrono::duration_cast<duration>(std::max(negotiated, std::chrono::milliseconds::zero()));
}
void quic_connection_lifecycle::received(time_point now) {
    if (state_ != quic_lifecycle_state::active) return;
    activity_ = std::max(activity_, now);
    first_send_ = true;
}
void quic_connection_lifecycle::emitted(bool possibly_sent, bool ack_eliciting, time_point now) {
    if (state_ != quic_lifecycle_state::active || !possibly_sent || !ack_eliciting || !first_send_) return;
    activity_ = std::max(activity_, now);
    first_send_ = false;
}
bool quic_connection_lifecycle::terminate(quic_lifecycle_state state, const quic_close_frame& frame, time_point now, duration pto) {
    if (state_ != quic_lifecycle_state::active) return false;
    application_ = frame.application;
    error_ = frame.error;
    frame_type_ = frame.frame_type;
    reason_size_ = std::min(frame.reason.size(), reason_.size());
    std::copy_n(frame.reason.begin(), reason_size_, reason_.begin());
    endpoint_ = after(now, three_pto(pto));
    state_ = state;
    respond_ = state == quic_lifecycle_state::closing;
    response_after_ = now;
    return true;
}
bool quic_connection_lifecycle::local_close(const quic_close_frame& frame, time_point now, duration pto) {
    return terminate(quic_lifecycle_state::closing, frame, now, pto);
}
bool quic_connection_lifecycle::peer_close(const quic_close_frame& frame, time_point now, duration pto) {
    if (state_ == quic_lifecycle_state::closing) {
        state_ = quic_lifecycle_state::draining;
        respond_ = false;
        return true;
    }
    return terminate(quic_lifecycle_state::draining, frame, now, pto);
}
bool quic_connection_lifecycle::stateless_reset(time_point now, duration pto) { return peer_close({}, now, pto); }
bool quic_connection_lifecycle::expire(time_point now, duration pto) {
    auto due = deadline(pto);
    if (!due || now < *due) return false;
    state_ = quic_lifecycle_state::closed;
    endpoint_.reset();
    respond_ = false;
    return true;
}
std::optional<time_point> quic_connection_lifecycle::deadline(duration pto) const {
    if (state_ == quic_lifecycle_state::closed) return {};
    if (state_ != quic_lifecycle_state::active) return endpoint_;
    if (idle_ == duration::zero()) return {};
    return after(activity_, std::max(idle_, three_pto(pto)));
}
quic_close_frame quic_connection_lifecycle::close_frame(bool application_keys) const {
    if (application_ && !application_keys) return {false, 0x0c, 0, {}};
    return {application_, error_, frame_type_, std::span(reason_).first(reason_size_)};
}
void quic_connection_lifecycle::closing_packet(time_point) {
    if (state_ == quic_lifecycle_state::closing) respond_ = true;
}
std::optional<time_point> quic_connection_lifecycle::response_deadline() const {
    if (state_ != quic_lifecycle_state::closing || !respond_ || responses_ >= max_close_responses || !endpoint_ || response_after_ >= *endpoint_) return {};
    return response_after_;
}
bool quic_connection_lifecycle::response_due(time_point now) const {
    return state_ == quic_lifecycle_state::closing && respond_ && responses_ < max_close_responses && now >= response_after_ && endpoint_ && now < *endpoint_;
}
void quic_connection_lifecycle::close_emitted(bool possibly_sent, time_point now, duration pto) {
    if (state_ != quic_lifecycle_state::closing || !possibly_sent) return;
    ++responses_;
    respond_ = false;
    response_after_ = after(now, std::max(pto, duration::zero()));
}
}  // namespace httpserver::detail
