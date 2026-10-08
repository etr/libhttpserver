/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <httpserver/detail/quic_stream_state.hpp>
namespace httpserver::detail {
namespace {
quic_stream_result check_size(std::optional<std::uint64_t> final, std::uint64_t highest, std::uint64_t end, bool fixes_size) {
    if ((final && (end > *final || (fixes_size && end != *final))) || (fixes_size && end < highest))
        return {quic_stream_code::final_size_error};
    return {};
}
}  // namespace
std::optional<quic_stream_identity> quic_stream_id(std::uint64_t id) {
    if (id > k_quic_max_integer) return {};
    return quic_stream_identity{(id & 1) ? quic_endpoint_role::server : quic_endpoint_role::client, static_cast<bool>(id & 2), id >> 2};
}
std::optional<std::uint64_t> quic_stream_ids::open_local(bool unidirectional) {
    const auto stream_class = (local_ == quic_endpoint_role::server ? 1 : 0) + (unidirectional ? 2 : 0);
    auto& count = counts_[stream_class];
    if (count == (std::uint64_t{1} << 60)) return {};
    return 4 * count++ + stream_class;
}
quic_stream_result quic_stream_ids::observe_peer(std::uint64_t id) {
    auto identity = quic_stream_id(id);
    if (!identity) return {quic_stream_code::frame_encoding_error};
    if (identity->initiator == local_) return {opened(id) ? quic_stream_code::ok : quic_stream_code::stream_state_error};
    auto& count = counts_[id & 3];
    count = std::max(count, identity->ordinal + 1);
    return {};
}
bool quic_stream_ids::opened(std::uint64_t id) const {
    return id <= k_quic_max_integer && (id >> 2) < counts_[id & 3];
}
std::uint64_t quic_stream_ids::opened_count(std::uint8_t stream_class) const {
    return stream_class < 4 ? counts_[stream_class] : 0;
}
quic_stream_state::quic_stream_state(std::uint64_t id, quic_endpoint_role local, const quic_stream_ids& ids,
                                   quic_stream_limits limits, server::resource_budget budget)
    : id_(id), reassembly_(limits, std::move(budget)) {
    if (ids.local_role() != local || !ids.opened(id)) throw std::invalid_argument("QUIC stream ID is not open for this connection owner");
    if (!quic_stream_sender_allowed(id, true, local)) send_ = quic_send_state::unavailable;
    if (!quic_stream_sender_allowed(id, false, local)) receive_ = quic_receive_state::unavailable;
}
quic_stream_result quic_stream_state::check_frame(std::uint64_t id, bool receive_half) const {
    if (id > k_quic_max_integer) return {quic_stream_code::frame_encoding_error};
    if (id != id_ || (receive_half ? receive_ == quic_receive_state::unavailable : send_ == quic_send_state::unavailable))
        return {quic_stream_code::stream_state_error};
    return {};
}
quic_stream_result quic_stream_state::check_final(std::uint64_t end, bool fixes_size) const {
    return check_size(final_, highest_received_, end, fixes_size);
}
void quic_stream_state::update_receive() {
    if (!final_) return;
    if (reassembly_.consumed() == *final_) {
        receive_ = quic_receive_state::data_consumed;
        if (!terminal_delivered_) terminal_ = quic_stream_terminal{quic_terminal_kind::eof};
    } else if (reassembly_.contiguous_bytes() == *final_ - reassembly_.consumed()) {
        receive_ = quic_receive_state::data_received;
    } else {
        receive_ = quic_receive_state::size_known;
    }
}
quic_stream_result quic_stream_state::receive(const quic_stream_frame& frame) {
    auto checked = check_frame(frame.stream, true);
    if (!checked) return checked;
    if (frame.offset > k_quic_max_integer || frame.data.size() > k_quic_max_integer - frame.offset)
        return {quic_stream_code::frame_encoding_error};
    const auto end = frame.offset + frame.data.size();
    checked = check_final(end, frame.fin);
    if (!checked) return checked;
    if (receive_ == quic_receive_state::reset_received || receive_ == quic_receive_state::reset_consumed) {
        highest_received_ = std::max(highest_received_, end);
        return {};
    }
    auto inserted = reassembly_.insert(frame.offset, frame.data);
    if (!inserted) return inserted;
    highest_received_ = std::max(highest_received_, end);
    if (frame.fin) final_ = end;
    update_receive();
    return {};
}
quic_stream_result quic_stream_state::receive(const quic_reset_stream_frame& frame) {
    auto checked = check_frame(frame.stream, true);
    if (!checked) return checked;
    if (frame.error > k_quic_max_integer || frame.final_size > k_quic_max_integer) return {quic_stream_code::frame_encoding_error};
    checked = check_final(frame.final_size, true);
    if (!checked) return checked;
    if (receive_ == quic_receive_state::reset_received || receive_ == quic_receive_state::reset_consumed) return {};
    final_ = frame.final_size;
    reassembly_.clear();
    // Reset interrupts unread data, but cannot replace an application terminal
    // already delivered. Repeated reset errors preserve the first accepted one.
    if (!terminal_delivered_) {
        receive_ = quic_receive_state::reset_received;
        terminal_ = quic_stream_terminal{quic_terminal_kind::reset, frame.error};
    }
    return {};
}
quic_stream_result quic_stream_state::receive(const quic_stop_sending_frame& frame) {
    auto checked = check_frame(frame.stream, false);
    if (!checked) return checked;
    if (frame.error > k_quic_max_integer) return {quic_stream_code::frame_encoding_error};
    if (reset_ || send_ == quic_send_state::data_acknowledged) return {};
    reset_ = quic_reset_stream_frame{id_, frame.error, highest_sent_};
    reset_request_pending_ = true;
    send_ = quic_send_state::reset_pending;
    return {};
}
quic_stream_read_result quic_stream_state::read(std::span<std::byte> output) {
    auto checked = check_frame(id_, true);
    if (!checked) return {checked.code, 0};
    if (receive_ == quic_receive_state::reset_received || receive_ == quic_receive_state::reset_consumed) return {};
    auto bytes = reassembly_.read(output);
    update_receive();
    return {quic_stream_code::ok, bytes};
}
quic_stream_result quic_stream_state::record_stream_sent(std::uint64_t offset, std::size_t length, bool fin) {
    auto checked = check_frame(id_, false);
    if (!checked) return checked;
    if (offset > k_quic_max_integer || length > k_quic_max_integer - offset) return {quic_stream_code::frame_encoding_error};
    if (reset_) return {quic_stream_code::stream_state_error};
    const auto end = offset + length;
    checked = check_size(sent_final_, highest_sent_, end, fin);
    if (!checked) return checked;
    highest_sent_ = std::max(highest_sent_, end);
    if (fin) sent_final_ = end;
    if (send_ != quic_send_state::data_acknowledged) send_ = sent_final_ ? quic_send_state::fin_sent : quic_send_state::sending;
    return {};
}
quic_stream_result quic_stream_state::record_reset_sent(const quic_reset_stream_frame& frame) {
    auto checked = check_frame(frame.stream, false);
    if (!checked) return checked;
    if (frame.error > k_quic_max_integer || frame.final_size > k_quic_max_integer) return {quic_stream_code::frame_encoding_error};
    if (frame.final_size != highest_sent_) return {quic_stream_code::final_size_error};
    if (reset_ && (reset_->error != frame.error || reset_->final_size != frame.final_size)) return {quic_stream_code::stream_state_error};
    if (send_ == quic_send_state::data_acknowledged) return {quic_stream_code::stream_state_error};
    reset_ = frame;
    reset_request_pending_ = false;
    if (send_ != quic_send_state::reset_acknowledged) send_ = quic_send_state::reset_sent;
    return {};
}
quic_stream_result quic_stream_state::acknowledge_all_stream_data() {
    if (send_ != quic_send_state::fin_sent && send_ != quic_send_state::data_acknowledged) return {quic_stream_code::stream_state_error};
    send_ = quic_send_state::data_acknowledged;
    return {};
}
quic_stream_result quic_stream_state::acknowledge_reset() {
    if (send_ != quic_send_state::reset_sent && send_ != quic_send_state::reset_acknowledged) return {quic_stream_code::stream_state_error};
    send_ = quic_send_state::reset_acknowledged;
    return {};
}
std::optional<quic_reset_stream_frame> quic_stream_state::take_reset_request() {
    if (!reset_request_pending_) return {};
    reset_request_pending_ = false;
    return reset_;
}
std::optional<quic_stream_terminal> quic_stream_state::take_terminal() {
    auto terminal = terminal_;
    terminal_.reset();
    if (terminal) {
        terminal_delivered_ = true;
        if (terminal->kind == quic_terminal_kind::reset) receive_ = quic_receive_state::reset_consumed;
    }
    return terminal;
}

quic_stream_state::quic_stream_state(std::uint64_t id, quic_endpoint_role local, const quic_stream_ids& ids,
                                   quic_stream_limits limits, quic_storage_lease storage)
    : quic_stream_state(id, local, ids, limits, storage.budget) { storage_owner_ = std::move(storage); }
}  // namespace httpserver::detail
