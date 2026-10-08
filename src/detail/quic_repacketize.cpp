/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <utility>
#include <vector>
#include "./quic_recovery_internal.hpp"
namespace httpserver::detail {
namespace {
bool can_split_payload(quic_codec_code code) {
    return code == quic_codec_code::no_space || code == quic_codec_code::limit_exceeded;
}
}  // namespace
quic_recovery::implementation::information* quic_recovery::implementation::find_information(quic_information_id id) {
    for (auto& value : information_records) if (value.id == id) return &value;
    return nullptr;
}
const quic_recovery::implementation::information* quic_recovery::implementation::find_information(quic_information_id id) const {
    for (const auto& value : information_records) if (value.id == id) return &value;
    return nullptr;
}
quic_information_result quic_recovery::implementation::retain(information value, std::span<const std::byte> data) {
    auto* s = state(value.space);
    if (!s) return {{quic_recovery_code::invalid}};
    if (!valid_information(value, data)) return {{quic_recovery_code::invalid}};
    if (s->discarded) return {{quic_recovery_code::discarded}};
    if (admission != quic_recovery_code::ok) return {{admission}};
    if (data.size() > config.max_retained_bytes - retained_bytes) return {{quic_recovery_code::capacity}};
    std::erase_if(information_records, [](const auto& i) { return i.cancelled || (i.completed && !i.completion_pending); });
    if (!information_capacity()) return {{quic_recovery_code::capacity}};
    return store_information(std::move(value), data);
}
bool quic_recovery::implementation::valid_information(const information& value, std::span<const std::byte> data) const {
    return value.offset <= k_quic_max_integer && data.size() <= k_quic_max_integer - value.offset;
}
bool quic_recovery::implementation::information_capacity() const {
    return information_records.size() < config.max_information && next_information != 0;
}
quic_information_result quic_recovery::implementation::store_information(information value, std::span<const std::byte> data) {
    try {
        if (!data.empty() && !budget.reserve(server::resource::quic_reassembly_bytes, 2 * data.size(), value.storage).ok())
            return {{quic_recovery_code::no_memory}};
        value.data.assign(data.begin(), data.end());
        value.status.assign(data.size(), information_status::pending);
        value.length = data.size();
        value.id = next_information;
        information_records.push_back(std::move(value));
        retained_bytes += data.size();
        return {{}, next_information++};
    } catch (const std::bad_alloc&) {
        return {{quic_recovery_code::no_memory}};
    }
}
quic_information_result quic_recovery::retain_crypto(quic_pn_space space, std::uint64_t offset, std::span<const std::byte> data) {
    if (data.empty()) return {{quic_recovery_code::invalid}};
    implementation::information value;
    value.space = space;
    value.offset = offset;
    return impl_->retain(std::move(value), data);
}
quic_information_result quic_recovery::retain_stream(const quic_stream_frame& frame) {
    if (frame.stream > k_quic_max_integer || (!frame.fin && frame.data.empty()) ||
        !quic_stream_sender_allowed(frame.stream, true, impl_->config.role)) return {{quic_recovery_code::invalid}};
    implementation::information value;
    value.kind = quic_information_kind::stream;
    value.space = quic_pn_space::application;
    value.stream = frame.stream;
    value.offset = frame.offset;
    value.fin = frame.fin;
    value.terminal = frame.fin ? information_status::pending : information_status::delivered;
    return impl_->retain(std::move(value), frame.data);
}
quic_information_result quic_recovery::retain_reset(const quic_reset_stream_frame& frame) {
    if (frame.stream > k_quic_max_integer || frame.error > k_quic_max_integer || frame.final_size > k_quic_max_integer ||
        !quic_stream_sender_allowed(frame.stream, true, impl_->config.role)) return {{quic_recovery_code::invalid}};
    implementation::information value;
    value.kind = quic_information_kind::reset_stream;
    value.space = quic_pn_space::application;
    value.stream = frame.stream;
    value.offset = frame.final_size;
    value.error = frame.error;
    value.terminal = information_status::pending;
    return impl_->retain(std::move(value), {});
}
void quic_recovery::implementation::release_storage(information& value) {
    retained_bytes -= value.data.size();
    std::vector<std::byte>().swap(value.data);
    std::vector<information_status>().swap(value.status);
    value.storage.release();
}
quic_recovery_result quic_recovery::cancel_information(quic_information_id id) {
    auto* value = impl_->find_information(id);
    if (!value) return {quic_recovery_code::invalid};
    value->cancelled = true;
    value->completion_pending = false;
    impl_->release_storage(*value);
    return {};
}
std::optional<std::uint64_t> quic_recovery::delivered_prefix(quic_information_id id) const {
    const auto* value = impl_->find_information(id);
    if (!value || value->cancelled) return {};
    if (value->completed) return value->offset + value->length;
    auto first = std::find_if(value->status.begin(), value->status.end(), [](auto status) { return status != information_status::delivered; });
    return value->offset + std::distance(value->status.begin(), first);
}
std::optional<quic_information_completion> quic_recovery::take_completion() {
    for (auto& value : impl_->information_records) {
        if (!value.completion_pending) continue;
        value.completion_pending = false;
        return quic_information_completion{value.id, value.kind, value.space, value.stream, value.offset + value.length, value.fin};
    }
    return {};
}
void quic_recovery::implementation::complete_information(information& value) {
    if (value.terminal != information_status::delivered || value.delivered_bytes != value.length) return;
    value.completed = value.completion_pending = true;
    release_storage(value);
}
void quic_recovery::implementation::update_information(slice content, information_status status) {
    auto* value = find_information(content.id);
    if (!value) return;
    if (value->cancelled || value->completed) return;
    for (auto i = content.start; i < content.start + content.length; ++i) {
        if (value->status[i] == information_status::delivered) continue;
        value->delivered_bytes += status == information_status::delivered;
        value->status[i] = status;
    }
    if (content.terminal && value->terminal != information_status::delivered) value->terminal = status;
    if (status == information_status::delivered) complete_information(*value);
}
std::optional<quic_recovery::implementation::slice> quic_recovery::implementation::select_information(quic_pn_space space, bool probe) const {
    for (const auto& value : information_records) {
        if (value.space != space || value.completed || value.cancelled) continue;
        auto eligible = [probe](auto status) { return status == information_status::pending || (probe && status == information_status::sent); };
        auto first = std::find_if(value.status.begin(), value.status.end(), eligible);
        auto last = std::find_if(first, value.status.end(), [&](auto status) { return !eligible(status); });
        bool terminal = eligible(value.terminal) && last == value.status.end();
        if (first == value.status.end() && !terminal) continue;
        return slice{value.id, static_cast<std::size_t>(std::distance(value.status.begin(), first)), static_cast<std::size_t>(std::distance(first, last)), terminal};
    }
    return {};
}
quic_frame quic_recovery::implementation::information_frame(const information& value, slice content) const {
    if (value.kind == quic_information_kind::reset_stream) return quic_reset_stream_frame{value.stream, value.error, value.offset};
    const auto bytes = std::span(value.data).subspan(content.start, content.length);
    const auto offset = value.offset + content.start;
    if (value.kind == quic_information_kind::crypto) return quic_crypto_frame{offset, bytes};
    return quic_stream_frame{value.stream, offset, bytes, content.terminal && value.fin, true, true};
}
quic_encode_result quic_recovery::implementation::encode_information(slice& content, std::span<std::byte> output) {
    auto& value = *find_information(content.id);
    const quic_frame_context context{value.space == quic_pn_space::initial ? quic_packet_kind::initial :
        value.space == quic_pn_space::handshake ? quic_packet_kind::handshake : quic_packet_kind::one_rtt, config.role};
    auto full = encode_quic_frame(information_frame(value, content), output, context);
    if (!can_split_payload(full.code) || !content.length) return full;
    const auto original = content;
    std::size_t lower = 0, upper = std::min(content.length, output.size());
    while (lower < upper) {
        auto middle = lower + (upper - lower + 1) / 2;
        slice attempt{content.id, content.start, middle, original.terminal && middle == original.length};
        auto result = encode_quic_frame(information_frame(value, attempt), output, context);
        if (result.code == quic_codec_code::ok) lower = middle;
        else upper = middle - 1;
    }
    if (!lower) return {quic_codec_code::no_space};
    content.length = lower;
    content.terminal = original.terminal && lower == original.length;
    return encode_quic_frame(information_frame(value, content), output, context);
}
quic_send_plan quic_recovery::prepare_packet(quic_pn_space space, std::span<std::byte> output, time_point now, bool probe) {
    auto plan = reserve_packet(space);
    if (!plan) return plan;
    auto content = impl_->select_information(space, false);
    if (!content && probe) content = impl_->select_information(space, true);
    if (content || probe) {
        auto encoded = impl_->encode_content(content, output);
        if (encoded.code != quic_codec_code::ok) {
            abandon_packet(plan.token);
            return {{quic_recovery_code::no_space}};
        }
        impl_->pending->content = content.value_or(implementation::slice{});
        plan.bytes = encoded.consumed;
        plan.ack_eliciting = true;
    }
    if (ack_deadline(space)) append_ack(plan, space, output, now);
    if (!plan.bytes) {
        abandon_packet(plan.token);
        return {{empty_plan_code(space)}};
    }
    impl_->pending->prepared = true;
    impl_->pending->ack_eliciting = plan.ack_eliciting;
    impl_->pending->payload_bytes = plan.bytes;
    return plan;
}
quic_recovery_code quic_recovery::empty_plan_code(quic_pn_space space) const {
    return ack_deadline(space) ? quic_recovery_code::no_space : quic_recovery_code::no_data;
}
quic_encode_result quic_recovery::implementation::encode_content(std::optional<slice>& content, std::span<std::byte> output) {
    return content ? encode_information(*content, output) : encode_quic_frame(quic_ping_frame{}, output);
}
void quic_recovery::append_ack(quic_send_plan& plan, quic_pn_space space, std::span<std::byte> output, time_point now) {
    auto ack = prepare_ack(space, output.subspan(plan.bytes), now);
    if (!ack) return;
    plan.bytes += ack.bytes;
    impl_->pending->ack_generation = ack.generation;
    impl_->pending->receive_watermark = impl_->state(space)->received.front().largest;
}
bool quic_recovery::implementation::can_collect(const packet& p) const {
    if (p.acknowledged || (!p.in_flight && !p.ack_eliciting)) return true;
    if (!p.lost) return false;
    auto* value = find_information(p.content.id);
    return !value || value->completed || value->cancelled;
}
void quic_recovery::implementation::retire_acknowledged_receive(space_state& s, const packet& p) {
    // A hole filled after preparation was not covered by the published ACK.
    if (p.receive_watermark && p.ack_generation.value_or(0) >= s.last_reordered_generation)
        retire_received(s, *p.receive_watermark);
}
void quic_recovery::implementation::retire_received(space_state& s, std::uint64_t watermark) {
    s.receive_floor = std::max(s.receive_floor, watermark + 1);
    std::erase_if(s.received, [&](auto range) { return range.largest <= watermark; });
    if (!s.received.empty()) s.received.back().smallest = std::max(s.received.back().smallest, s.receive_floor);
    else s.ack_due.reset();
}
}  // namespace httpserver::detail
