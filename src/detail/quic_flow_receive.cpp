/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include "./quic_flow_internal.hpp"
namespace httpserver::detail {
quic_flow_result quic_flow_control::implementation::receive_admission(const quic_stream_state& stream, std::uint64_t id, std::uint64_t end, record*& value) {
    if (id != stream.id() || !ids.opened(id) || !quic_stream_sender_allowed(id, false, ids.local_role())) return {quic_flow_code::stream_state_error};
    auto admitted = record_admission(id);
    if (admitted != quic_flow_code::ok) return {admitted};
    value = create(id);
    if (value->receive_owner && value->receive_owner != &stream) return {quic_flow_code::stream_state_error};
    const auto delta = end > value->received ? end - value->received : 0;
    if (end > value->receive_limit.desired || delta > receive_limit.desired - received) return {quic_flow_code::flow_control_error};
    return {};
}
quic_flow_result quic_flow_control::receive(quic_stream_state& stream, const quic_stream_frame& frame) {
    if (frame.offset > k_quic_max_integer || frame.data.size() > k_quic_max_integer - frame.offset) return {quic_flow_code::frame_encoding_error};
    const auto end = frame.offset + frame.data.size();
    implementation::record* value = nullptr;
    auto admitted = impl_->receive_admission(stream, frame.stream, end, value);
    if (!admitted) return admitted;
    auto accepted = stream.receive(frame);
    if (!accepted) return flow_result(accepted);
    impl_->received += end > value->received ? end - value->received : 0;
    value->received = std::max(value->received, end);
    value->receive_owner = &stream;
    return {};
}
quic_flow_result quic_flow_control::receive(quic_stream_state& stream, const quic_reset_stream_frame& frame) {
    if (frame.final_size > k_quic_max_integer) return {quic_flow_code::frame_encoding_error};
    implementation::record* value = nullptr;
    auto admitted = impl_->receive_admission(stream, frame.stream, frame.final_size, value);
    if (!admitted) return admitted;
    auto accepted = stream.receive(frame);
    if (!accepted) return flow_result(accepted);
    impl_->received += frame.final_size > value->received ? frame.final_size - value->received : 0;
    value->received = std::max(value->received, frame.final_size);
    value->receive_owner = &stream;
    return {};
}
void quic_flow_control::implementation::release(record& value, std::uint64_t through, bool stream_credit) {
    const auto delta = through - value.released;
    released += delta;
    value.released = through;
    receive_limit.desired = flow_add(receive_window, released);
    if (stream_credit) value.receive_limit.desired = flow_add(value.receive_window, value.released);
}
quic_flow_result quic_flow_control::implementation::consume(const quic_stream_state& stream, std::uint64_t through) {
    auto* value = find(stream.id());
    if (!value || value->receive_owner != &stream || value->retired || value->reset_settled ||
        stream.receive_state() == quic_receive_state::reset_received || stream.receive_state() == quic_receive_state::reset_consumed)
        return {quic_flow_code::stream_state_error};
    if (through < value->released || through > stream.consumed() || through > value->received) return {quic_flow_code::invalid_consumption};
    release(*value, through, true);
    return {};
}
quic_flow_result quic_flow_control::consume_body(const quic_stream_state& stream, std::uint64_t through) { return impl_->consume(stream, through); }
quic_flow_result quic_flow_control::consume_protocol(const quic_stream_state& stream, std::uint64_t through) { return impl_->consume(stream, through); }
quic_flow_result quic_flow_control::settle_reset(const quic_stream_state& stream) {
    auto* value = impl_->find(stream.id());
    if (!value || value->receive_owner != &stream || (stream.receive_state() != quic_receive_state::reset_received &&
        stream.receive_state() != quic_receive_state::reset_consumed)) return {quic_flow_code::stream_state_error};
    if (!value->reset_settled) impl_->release(*value, value->received, false);
    value->reset_settled = true;
    value->receive_limit.acknowledged = value->receive_limit.desired;
    return {};
}
namespace {
bool receive_done(const quic_stream_state& stream, bool settled, std::uint64_t released, std::uint64_t received) {
    return stream.receive_state() == quic_receive_state::unavailable || settled ||
        (stream.receive_state() == quic_receive_state::data_consumed && released == received);
}
bool send_done(const quic_stream_state& stream) {
    return stream.send_state() == quic_send_state::unavailable || stream.send_state() == quic_send_state::data_acknowledged ||
        stream.send_state() == quic_send_state::reset_acknowledged;
}
}  // namespace
quic_flow_result quic_flow_control::retire(const quic_stream_state& stream) {
    auto* value = impl_->find(stream.id());
    if (!value) return {quic_flow_code::stream_state_error};
    if (value->receive_owner && value->receive_owner != &stream) return {quic_flow_code::stream_state_error};
    if (value->retired) return {};
    if (!receive_done(stream, value->reset_settled, value->released, value->received) || !send_done(stream)) return {quic_flow_code::stream_state_error};
    value->retired = true;
    if (quic_stream_id(stream.id())->initiator != impl_->ids.local_role() && impl_->records.size() < impl_->maximum) {
        auto& credit = impl_->receive_counts[(stream.id() & 2) != 0];
        credit.desired = std::min(credit.desired + 1, std::uint64_t{1} << 60);
    }
    return {};
}
quic_flow_control::implementation::credit* quic_flow_control::implementation::control(const quic_flow_frame& frame) {
    switch (frame.kind) {
    case quic_flow_kind::max_data: return &receive_limit;
    case quic_flow_kind::max_stream_data: {
        auto* value = find(frame.stream);
        return value && !value->reset_settled ? &value->receive_limit : nullptr;
    }
    case quic_flow_kind::max_streams_bidi: return &receive_counts[0];
    case quic_flow_kind::max_streams_uni: return &receive_counts[1];
    default: return nullptr;
    }
}
std::optional<quic_flow_frame> quic_flow_control::pending_credit() const {
    if (impl_->receive_limit.desired > impl_->receive_limit.acknowledged) return quic_flow_frame{quic_flow_kind::max_data, impl_->receive_limit.desired};
    for (const auto& value : impl_->records)
        if (!value.retired && !value.reset_settled && value.receive_limit.desired > value.receive_limit.acknowledged)
            return quic_flow_frame{quic_flow_kind::max_stream_data, value.receive_limit.desired, value.id};
    for (unsigned i = 0; i < 2; ++i)
        if (impl_->receive_counts[i].desired > impl_->receive_counts[i].acknowledged)
            return quic_flow_frame{i ? quic_flow_kind::max_streams_uni : quic_flow_kind::max_streams_bidi, impl_->receive_counts[i].desired};
    return {};
}
std::optional<quic_flow_frame> quic_flow_control::pending_credit(quic_flow_kind kind, std::uint64_t id) const {
    quic_flow_frame frame{kind, 0, id};
    const auto* credit = impl_->control(frame);
    if (!credit || credit->desired <= credit->acknowledged) return {};
    if (kind == quic_flow_kind::max_stream_data && impl_->find(id)->retired) return {};
    frame.limit = credit->desired;
    return frame;
}
void quic_flow_control::credit_emitted(const quic_flow_frame& frame) {
    auto* credit = impl_->control(frame);
    if (credit && frame.limit <= credit->desired) credit->emitted = std::max(credit->emitted, frame.limit);
}
void quic_flow_control::credit_acknowledged(const quic_flow_frame& frame) {
    auto* credit = impl_->control(frame);
    if (credit && frame.limit <= credit->desired) credit->acknowledged = std::max(credit->acknowledged, frame.limit);
}
}  // namespace httpserver::detail
