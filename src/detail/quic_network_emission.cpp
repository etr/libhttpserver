/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdexcept>
#include <httpserver/detail/quic_network_connection.hpp>
namespace httpserver::detail {
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using time_point = quic_network_connection::clock_type::time_point;
void include_deadline(std::optional<time_point>& next, std::optional<time_point> due) {
    if (due && (!next || *due < *next)) next = due;
}
}  // namespace
void quic_network_connection::complete_close(quic_identity_emission outcome, clock_type::time_point now) {
    if (outcome != quic_identity_emission::unsent) {
        require(pending_budget_->complete_send(*debit_), "terminal amplification debit");
        lifecycle_.close_emitted(true, now, recovery_.current_pto());
    } else {
        pending_budget_->cancel_unsent(*debit_);
    }
    close_pending_ = false;
    debit_.reset(); pending_budget_.reset(); retire_terminal(now);
}
void quic_network_connection::complete_control(quic_identity_emission outcome, clock_type::time_point now) {
    const auto completed = identity_->complete_control(pending_control_->token, outcome, now, keys_.generation(quic_key_direction::write));
    pending_control_.reset();
    lifecycle_.emitted(outcome != quic_identity_emission::unsent, true, now);
    if (!completed) request_close({false, 0x01, 0, {}}, now);
    retire_terminal(now);
}
void quic_network_connection::record_plan_sent(const quic_send_plan& plan, quic_pn_space space, clock_type::time_point now) {
    if (plan.stream) {
        require(static_cast<bool>(streams_.at(plan.stream->stream)->record_stream_sent(plan.stream->offset, plan.stream->data.size(), plan.stream->fin)), "stream actual send");
        engine_->sent(plan.stream->stream, plan.stream->offset, plan.stream->data.size(), plan.stream->fin, now);
    }
    if (plan.reset) require(static_cast<bool>(streams_.at(plan.reset->stream)->record_reset_sent(*plan.reset)), "reset actual send");
    if (plan.flow) flow_->credit_emitted(*plan.flow);
    auto index = static_cast<unsigned>(space);
    if (pending_batch_ && probes_[index] && plan.ack_eliciting) --probes_[index];
}
quic_identity_result quic_network_connection::commit_prepared(quic_identity_emission outcome, clock_type::time_point now) {
    require(pending_budget_->complete_send(*debit_), "actual amplification debit");
    const auto committed = pending_batch_ ? recovery_.commit_datagram(std::span(pending_packets_).first(pending_count_), now) :
        recovery_.commit_sent(pending_->token, now, pending_bytes_, true, true, keys_.generation(quic_key_direction::write));
    require(static_cast<bool>(committed), "actual UDP send commit");
    for (std::size_t i = 0; active() && i < pending_count_; ++i) record_plan_sent(pending_plans_[i], pending_spaces_[i], now);
    bool eliciting = !pending_batch_;
    for (std::size_t i = 0; i < pending_count_; ++i) eliciting |= pending_packets_[i].ack_eliciting;
    lifecycle_.emitted(true, eliciting, now);
    return identity_->peer_cid_emitted(identity_->application_cid(), identity_->application_path().peer, outcome);
}
void quic_network_connection::abandon_prepared() {
    if (pending_batch_) recovery_.abandon_datagram();
    else recovery_.abandon_packet(pending_->token);
    pending_budget_->cancel_unsent(*debit_);
}
void quic_network_connection::emitted(bool success, clock_type::time_point now) {
    emitted(success ? quic_identity_emission::sent : quic_identity_emission::unsent, now);
}
void quic_network_connection::emitted(quic_identity_emission outcome, clock_type::time_point now) {
    if (close_pending_) {
        complete_close(outcome, now);
        return;
    }
    if (pending_control_) {
        complete_control(outcome, now);
        return;
    }
    if (!pending_ && !active()) return;
    require(pending_.has_value(), "send without preparation");
    quic_identity_result reset_registration;
    if (outcome != quic_identity_emission::unsent) reset_registration = commit_prepared(outcome, now);
    else abandon_prepared();
    pending_count_ = 0; pending_.reset(); debit_.reset(); pending_budget_.reset();
    if (!reset_registration) request_close({false, 0x01, 0, {}}, now);
    retire_terminal(now);
}
std::optional<quic_network_connection::clock_type::time_point> quic_network_connection::deadline() const {
    if (failed_) return {};
    if (!active()) {
        auto due = lifecycle_.deadline(recovery_.current_pto());
        if (cached_close_ && !close_pending_ && !close_blocked_) include_deadline(due, lifecycle_.response_deadline());
        return due;
    }
    auto due = pending() ? std::nullopt : recovery_.next_send_deadline();
    include_deadline(due, identity_->deadline());
    include_deadline(due, lifecycle_.deadline(recovery_.current_pto()));
    if (engine_) {
        include_deadline(due, engine_->drain_deadline());
        include_deadline(due, engine_->next_deadline());
    }
    if (auto loss = recovery_.next_deadline()) include_deadline(due, loss->deadline);
    if (!connected_) include_deadline(due, handshake_deadline_);
    return due;
}
}  // namespace httpserver::detail
