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
bool http3_connection::qpack_input(stream& s, std::byte byte, std::uint64_t id) {
    try {
        const bool encoder = s.role == http3_role::qpack_encoder;
        if (!s.instruction_charge.owns()) {
            const auto capacity = encoder ? limits_.qpack_instruction_bytes : 10;
            if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, encoder ? 3 * capacity : capacity, s.instruction_charge).ok()) {
                fail(0x107, "QPACK instruction storage exhausted", id); return false;
            }
            s.instruction.reserve(capacity);
        }
        if (s.instruction.size() == (encoder ? limits_.qpack_instruction_bytes : 10)) {
            fail(encoder ? 0x107 : 0x202, "QPACK instruction exceeds bounds", id); return false;
        }
        s.instruction.push_back(std::to_integer<std::uint8_t>(byte));
        if (encoder) {
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
            s.instruction.clear(); --frames_left_;
            if (!unblock_sections()) return false;
            if (decoder_table_.insert_count() > reported_inserts_) {
                if (!queue_feedback({qpack_decoder_instruction_kind::insert_count_increment, decoder_table_.insert_count() - reported_inserts_})) return false;
                reported_inserts_ = decoder_table_.insert_count();
            }
        } else {
            auto decoded = qpack_decode_decoder_instruction(s.instruction);
            if (decoded.status.state == qpack_state::incomplete) return true;
            if (!decoded.status.ok() || !apply_decoder_instruction(decoded.instruction)) {
                fail(0x202, "Decoder instruction rejected", id); return false;
            }
            s.instruction.clear(); --frames_left_;
        }
        return true;
    } catch (const std::bad_alloc&) { fail(0x102, "QPACK instruction allocation failed", id); return false;
    } catch (const std::length_error&) { fail(0x102, "QPACK instruction size invalid", id); return false; }
}
bool http3_connection::apply_decoder_instruction(qpack_decoder_instruction instruction) {
    using kind = qpack_decoder_instruction_kind;
    if (instruction.kind == kind::stream_cancellation) {
        outgoing_.remove_if([&](const auto& section) { return section.stream == instruction.value; });
        return true;
    }
    if (instruction.kind == kind::insert_count_increment)
        return encoder_table_.increment_known_received(instruction.value).ok();
    const auto section = std::find_if(outgoing_.begin(), outgoing_.end(), [&](const auto& s) { return s.stream == instruction.value; });
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
    if (error_ || !peer_.received) return {qpack_state::invalid_argument};
    try {
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
    } catch (const std::bad_alloc&) { return {qpack_state::limit_exceeded};
    } catch (const std::length_error&) { return {qpack_state::limit_exceeded}; }
}
http3_encoded_section http3_connection::encode_section(std::uint64_t id, std::span<const qpack_field_view> fields) {
    const auto stream = streams_.find(id);
    if (error_ || !peer_.received || stream == streams_.end() || stream->second->role != http3_role::request)
        return {{qpack_state::invalid_argument}, {}, {}, {}};
    if (fields.size() > limits_.headers.max_fields) return {{qpack_state::limit_exceeded}, {}, {}, {}};
    try {
        server::reservation metadata;
        const auto bytes = sizeof(outgoing_section) + 128 + fields.size() * sizeof(std::uint64_t) * 4;
        if (!critical_.budget.reserve(server::resource::quic_reassembly_bytes, bytes, metadata).ok())
            return {{qpack_state::limit_exceeded}, {}, {}, {}};
        http3_encoded_section result; result.owner = data_;
        if (!data_.budget.reserve(server::resource::quic_reassembly_bytes, 2 * limits_.headers.max_compressed_bytes, result.charge).ok())
            return {{qpack_state::limit_exceeded}, {}, {}, {}};
        auto encoded = encoder_.encode_dynamic(fields, encoder_table_, encoder_table_.insert_count(), limits_.headers);
        if (!encoded.status.ok()) return {encoded.status, {}, {}, {}};
        if (encoded.required_insert_count > encoder_table_.known_received_count() &&
            !potentially_blocked(id) && outgoing_blocked() >= peer_.qpack_blocked) {
            encoded.references.release(); std::string().swap(encoded.value);
            auto safe = encoder_.encode(fields, limits_.headers);
            if (!safe.status.ok()) return {safe.status, {}, {}, {}};
            result.value = std::move(safe.value); return result;
        }
        if (encoded.required_insert_count) {
            if (outgoing_.size() == limits_.qpack_outstanding_sections) return {{qpack_state::limit_exceeded}, {}, {}, {}};
            outgoing_.push_back({id, encoded.required_insert_count, std::move(metadata), std::move(encoded.references)});
        }
        result.value = std::move(encoded.value); return result;
    } catch (const std::bad_alloc&) { return {{qpack_state::limit_exceeded}, {}, {}, {}};
    } catch (const std::length_error&) { return {{qpack_state::limit_exceeded}, {}, {}, {}}; }
}
}  // namespace httpserver::detail
