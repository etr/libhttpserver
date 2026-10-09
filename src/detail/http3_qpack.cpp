/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/http3_connection.hpp>
namespace httpserver::detail {
bool http3_connection::initialize_qpack() {
    try {
        if (!decoder_table_.initialize(data_.ancestors, limits_.qpack_capacity).ok() ||
            (limits_.qpack_capacity && !admit_qpack_output())) {
            fail(0x107, "QPACK connection storage exhausted", 0); return false;
        }
        std::vector<std::byte> payload;
        auto append = [](std::vector<std::byte>& out, std::uint64_t value) {
            std::array<std::byte, 8> wire{}; auto r = encode_quic_varint(value, wire);
            out.insert(out.end(), wire.begin(), wire.begin() + r.consumed);
        };
        append(payload, 1); append(payload, limits_.qpack_capacity);
        append(payload, 7); append(payload, limits_.qpack_blocked_streams);
        append(control_prefix_, 0); append(control_prefix_, 4); append(control_prefix_, payload.size());
        control_prefix_.insert(control_prefix_.end(), payload.begin(), payload.end());
        return true;
    } catch (const std::bad_alloc&) { fail(0x102, "QPACK storage allocation failed", 0); return false; }
}
bool http3_connection::admit_qpack_output() {
    if (output_charge_.owns()) return true;
    server::reservation charge;
    if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, 2 * limits_.qpack_output_bytes, charge).ok()) return false;
    std::array<std::vector<std::byte>, 2> prepared;
    for (auto& output : prepared) output.reserve(limits_.qpack_output_bytes);
    qpack_output_ = std::move(prepared); output_charge_ = std::move(charge);
    return true;
}
bool http3_connection::queue_feedback(qpack_decoder_instruction instruction) {
    auto wire = qpack_encode_decoder_instruction(instruction, 10);
    auto& output = qpack_output_[1];
    if (!wire.status.ok() || wire.value.size() > limits_.qpack_output_bytes - output.size()) {
        fail(0x107, "QPACK feedback output exhausted", 0); return false;
    }
    for (auto byte : wire.value) output.push_back(static_cast<std::byte>(byte));
    return true;
}
void http3_connection::release_retained(stream& s) {
    retained_bytes_ -= s.retained_bytes; s.retained_bytes = 0;
    if (s.blocked) {
        --blocked_streams_; s.blocked = false; s.blocked_charge.release();
    }
}
bool http3_connection::unblock_sections() {
    for (auto& [id, ptr] : streams_) {
        auto& s = *ptr;
        if (!s.blocked || s.section.required > decoder_table_.insert_count()) continue;
        if (!process_safe(s, id)) return false;
    }
    return true;
}
std::size_t http3_connection::instruction_capacity(const stream& s) const {
    return s.role == http3_role::qpack_encoder ? limits_.qpack_instruction_bytes : 10;
}
bool http3_connection::qpack_input(stream& s, std::byte byte, std::uint64_t id) {
    try {
        const bool encoder = s.role == http3_role::qpack_encoder;
        const auto capacity = instruction_capacity(s);
        if (!s.instruction_charge.owns()) {
            const auto storage = encoder ? 3 * capacity : capacity;
            if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, storage, s.instruction_charge).ok()) {
                fail(0x107, "QPACK instruction storage exhausted", id); return false;
            }
            s.instruction.reserve(capacity);
        }
        if (s.instruction.size() == capacity) {
            fail(encoder ? 0x107 : 0x202, "QPACK instruction exceeds bounds", id); return false;
        }
        s.instruction.push_back(std::to_integer<std::uint8_t>(byte));
        return encoder ? encoder_input(s, id) : decoder_input(s, id);
    } catch (const std::bad_alloc&) { fail(0x102, "QPACK instruction allocation failed", id); return false;
    } catch (const std::length_error&) { fail(0x102, "QPACK instruction size invalid", id); return false; }
}
bool http3_connection::encoder_input(stream& s, std::uint64_t id) {
    const qpack_instruction_limits limits{limits_.qpack_instruction_bytes, limits_.qpack_instruction_bytes, limits_.qpack_instruction_bytes};
    auto decoded = qpack_decode_encoder_instruction_allocated(s.instruction, limits);
    if (decoded.status.state == qpack_state::incomplete) return true;
    if (!decoded.status.ok()) {
        fail(decoded.status.state == qpack_state::limit_exceeded ? 0x107 : 0x201, "Encoder instruction rejected", id); return false;
    }
    const auto applied = qpack_apply_encoder_instruction_allocated(decoded.instruction, limits, decoder_table_);
    if (!applied.ok()) {
        fail(applied.state == qpack_state::limit_exceeded ? 0x107 : 0x201, "Encoder instruction rejected", id); return false;
    }
    s.instruction.clear(); --frame_budget(s);
    if (!unblock_sections()) return false;
    if (decoder_table_.insert_count() > reported_inserts_) {
        if (!queue_feedback({qpack_decoder_instruction_kind::insert_count_increment, decoder_table_.insert_count() - reported_inserts_})) return false;
        reported_inserts_ = decoder_table_.insert_count();
    }
    return true;
}
bool http3_connection::decoder_input(stream& s, std::uint64_t id) {
    auto decoded = qpack_decode_decoder_instruction(s.instruction);
    if (decoded.status.state == qpack_state::incomplete) return true;
    if (!decoded.status.ok() || !apply_decoder_instruction(decoded.instruction)) {
        fail(0x202, "Decoder instruction rejected", id); return false;
    }
    s.instruction.clear(); --frame_budget(s);
    return true;
}
bool http3_connection::apply_decoder_instruction(qpack_decoder_instruction instruction) {
    using kind = qpack_decoder_instruction_kind;
    if (instruction.kind == kind::stream_cancellation) {
        outgoing_.remove_if([&](const auto& section) { return section.stream == instruction.value; });
        return true;
    }
    if (instruction.kind == kind::insert_count_increment)
        return encoder_table_.increment_known_received(instruction.value).ok();
    const auto section = std::find_if(outgoing_.begin(), outgoing_.end(), [&](const auto& s) { return s.stream == instruction.value && s.published; });
    if (section == outgoing_.end()) return false;
    if (!encoder_table_.advance_known_received(std::max(encoder_table_.known_received_count(), section->required)).ok()) return false;
    outgoing_.erase(section); return true;
}
bool http3_connection::potentially_blocked(std::uint64_t id) const {
    return std::any_of(outgoing_.begin(), outgoing_.end(), [&](const auto& section) {
        return section.stream == id && section.required > encoder_table_.known_received_count();
    });
}
std::size_t http3_connection::outgoing_blocked() const {
    std::size_t count = 0;
    for (const auto& [id, s] : streams_) if (potentially_blocked(id)) ++count;
    return count;
}
qpack_status http3_connection::queue_encoder_instruction(const qpack_encoder_instruction& instruction) {
    if (stopped_ || error_ || !peer_.received) return {qpack_state::invalid_argument};
    try {
        return queue_encoder_allocated(instruction);
    } catch (const std::bad_alloc&) { return {qpack_state::limit_exceeded};
    } catch (const std::length_error&) { return {qpack_state::limit_exceeded}; }
}
qpack_status http3_connection::queue_encoder_allocated(const qpack_encoder_instruction& instruction) {
    if (!admit_qpack_output()) return {qpack_state::limit_exceeded};
    const qpack_instruction_limits limits{limits_.qpack_instruction_bytes, limits_.qpack_instruction_bytes, limits_.qpack_instruction_bytes};
    server::reservation scratch;
    if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, 2 * limits_.qpack_instruction_bytes, scratch).ok())
        return {qpack_state::limit_exceeded};
    auto wire = qpack_encode_encoder_instruction(instruction, limits);
    if (!wire.status.ok()) return wire.status;
    auto& output = qpack_output_[0];
    if (wire.value.size() > limits_.qpack_output_bytes - output.size()) return {qpack_state::limit_exceeded};
    const auto status = qpack_apply_encoder_instruction(instruction, limits, encoder_table_);
    if (!status.ok()) return status;
    for (auto byte : wire.value) output.push_back(static_cast<std::byte>(byte));
    return {};
}
http3_encoded_section http3_connection::encode_section(std::uint64_t id, std::span<const qpack_field_view> fields) {
    return encode_section_impl(id, fields, false);
}
bool http3_connection::section_stream(std::uint64_t id) const {
    const auto stream = streams_.find(id);
    return !stopped_ && !error_ && peer_.received && stream != streams_.end() && stream->second->role == http3_role::request;
}
http3_encoded_section http3_connection::encode_section_impl(std::uint64_t id, std::span<const qpack_field_view> fields, bool provisional) {
    if (!section_stream(id)) return {{qpack_state::invalid_argument}, {}, {}, {}};
    if (fields.size() > limits_.headers.max_fields) return {{qpack_state::limit_exceeded}, {}, {}, {}};
    try {
        return encode_section_allocated(id, fields, provisional);
    } catch (const std::bad_alloc&) { return {{qpack_state::limit_exceeded}, {}, {}, {}};
    } catch (const std::length_error&) { return {{qpack_state::limit_exceeded}, {}, {}, {}}; }
}
bool http3_connection::section_may_block(std::uint64_t id, std::uint64_t required) const {
    return required > encoder_table_.known_received_count() && !potentially_blocked(id) && outgoing_blocked() >= peer_.qpack_blocked;
}
bool http3_connection::retain_section(std::uint64_t id, qpack_dynamic_bytes_result& encoded, server::reservation charge, bool provisional, std::uint64_t& token) {
    if (!encoded.required_insert_count) return true;
    if (outgoing_.size() == limits_.qpack_outstanding_sections || next_section_ == 0) return false;
    outgoing_.push_back({id, encoded.required_insert_count, std::move(charge), std::move(encoded.references), next_section_++, !provisional});
    if (provisional) token = outgoing_.back().token;
    return true;
}
http3_encoded_section http3_connection::encode_section_allocated(std::uint64_t id, std::span<const qpack_field_view> fields, bool provisional) {
    server::reservation metadata;
    const auto bytes = sizeof(outgoing_section) + 128 + fields.size() * sizeof(std::uint64_t) * 4;
    if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, bytes, metadata).ok())
        return {{qpack_state::limit_exceeded}, {}, {}, {}};
    http3_encoded_section result; result.owner = data_;
    if (!data_.budget.reserve(server::resource::quic_reassembly_bytes, 2 * limits_.headers.max_compressed_bytes, result.charge).ok())
        return {{qpack_state::limit_exceeded}, {}, {}, {}};
    auto encoded = encoder_.encode_dynamic(fields, encoder_table_, encoder_table_.insert_count(), limits_.headers, false, peer_.qpack_capacity);
    if (!encoded.status.ok()) return {encoded.status, {}, {}, {}};
    if (section_may_block(id, encoded.required_insert_count)) {
        encoded.references.release(); std::string().swap(encoded.value);
        auto safe = encoder_.encode(fields, limits_.headers);
        if (!safe.status.ok()) return {safe.status, {}, {}, {}};
        result.value = std::move(safe.value); return result;
    }
    if (!retain_section(id, encoded, std::move(metadata), provisional, result.token)) return {{qpack_state::limit_exceeded}, {}, {}, {}};
    result.value = std::move(encoded.value); return result;
}
http3_encoded_section http3_connection::encode_response_section(std::uint64_t id, std::span<const qpack_field_view> fields) {
    if (stopped_ || error_) return {{qpack_state::invalid_argument}, {}, {}, {}};
    if (!peer_.received) {
        http3_encoded_section result; result.owner = data_;
        if (!data_.budget.reserve(server::resource::quic_reassembly_bytes, 2 * limits_.headers.max_compressed_bytes, result.charge).ok())
            return {{qpack_state::limit_exceeded}, {}, {}, {}};
        auto encoded = encoder_.encode(fields, limits_.headers);
        result.status = encoded.status; result.value = std::move(encoded.value); return result;
    }
    const auto capacity = encoder_table_.maximum_capacity();
    if (capacity && !encoder_table_.capacity())
        queue_encoder_instruction({qpack_encoder_instruction_kind::capacity, capacity});
    if (capacity) insert_response_names(fields);
    return encode_section_impl(id, fields, true);
}
void http3_connection::insert_response_names(std::span<const qpack_field_view> fields) {
    const auto count = std::min(fields.size(), limits_.headers.max_fields);
    for (const auto& field : fields.first(count)) {
        if (qpack_static_find_name(field.name) || encoder_table_.find_name(field.name)) continue;
        if (field.name.size() > limits_.qpack_instruction_bytes) continue;
        // Retain names only; semantic values remain never-indexed literals.
        queue_encoder_instruction({qpack_encoder_instruction_kind::insert_literal, 0, false, std::string(field.name), {}});
    }
}
void http3_connection::publish_section(std::uint64_t token) {
    for (auto& section : outgoing_)
        if (section.token == token) {
            section.published = true; return;
        }
}
void http3_connection::cancel_section(std::uint64_t token) {
    outgoing_.remove_if([&](const auto& section) { return section.token == token && !section.published; });
}
void http3_connection::cancel_unpublished(std::uint64_t id) {
    outgoing_.remove_if([&](const auto& section) { return section.stream == id && !section.published; });
}
bool http3_connection::blocked(std::uint64_t id) const {
    const auto it = streams_.find(id);
    return it != streams_.end() && it->second->blocked;
}
void http3_connection::shutdown() {
    if (stopped_) return;
    stopped_ = true;
    outgoing_.clear();
    streams_.clear();
    retained_bytes_ = blocked_streams_ = 0;
    for (auto& output : qpack_output_) std::vector<std::byte>().swap(output);
    std::vector<std::byte>().swap(control_prefix_);
    output_charge_.release(); metadata_.release();
    encoder_table_.clear(); decoder_table_.clear();
}
}  // namespace httpserver::detail
