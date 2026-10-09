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

#if !defined(HTTPSERVER_COMPILATION)
#error "qpack_instructions.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QPACK_INSTRUCTIONS_HPP_
#define SRC_HTTPSERVER_DETAIL_QPACK_INSTRUCTIONS_HPP_
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <httpserver/detail/qpack_dynamic_table.hpp>
#include <httpserver/detail/qpack_field_section.hpp>
namespace httpserver::detail {
struct qpack_instruction_limits {
    std::size_t max_encoded_bytes;
    std::size_t max_name_bytes;
    std::size_t max_value_bytes;
};
enum class qpack_encoder_instruction_kind { capacity, insert_name, insert_literal, duplicate };
struct qpack_encoder_instruction {
    qpack_encoder_instruction_kind kind = qpack_encoder_instruction_kind::capacity;
    std::uint64_t index = 0;
    bool static_name = false;
    std::string name;
    std::string value;
};
struct qpack_encoder_instruction_result {
    qpack_status status;
    std::size_t consumed = 0;
    qpack_encoder_instruction instruction;
};
inline qpack_encoder_instruction_result qpack_decode_encoder_instruction(std::span<const std::uint8_t> input,
                                                                         qpack_instruction_limits limits) {
    if (input.empty()) return {{qpack_state::incomplete}, 0, {}};
    using kind = qpack_encoder_instruction_kind;
    qpack_encoder_instruction instruction;
    const auto bits = input[0];
    unsigned prefix = 5;
    if (bits & 0x80) {
        instruction.kind = kind::insert_name;
        prefix = 6;
        instruction.static_name = (bits & 0x40) != 0;
    } else if (bits & 0x40) {
        instruction.kind = kind::insert_literal;
        prefix = 5;
    } else {
        instruction.kind = (bits & 0x20) ? kind::capacity : kind::duplicate;
    }
    std::size_t consumed = 0;
    std::span<const std::uint8_t> name_wire, value_wire;
    if (instruction.kind == kind::insert_literal) {
        const auto name = qpack_inspect_string(input, {limits.max_encoded_bytes, limits.max_name_bytes}, 5, 0x20);
        if (!name.status.ok()) return {name.status, 0, {}};
        name_wire = input.first(name.consumed);
        consumed = name.consumed;
    } else {
        const auto number = qpack_decode_integer(input, prefix, {});
        if (!number.status.ok()) return {number.status, 0, {}};
        consumed = number.consumed;
        instruction.index = number.value;
        if (instruction.kind == kind::insert_name && instruction.static_name && !qpack_static_lookup(number.value)) {
            return {{qpack_state::malformed}, 0, {}};
        }
    }
    if (consumed > limits.max_encoded_bytes) return {{qpack_state::limit_exceeded}, 0, {}};
    if (instruction.kind == kind::insert_literal || instruction.kind == kind::insert_name) {
        const auto value = qpack_inspect_string(input.subspan(consumed), {limits.max_encoded_bytes - consumed, limits.max_value_bytes});
        if (!value.status.ok()) return {value.status, 0, {}};
        value_wire = input.subspan(consumed, value.consumed);
        consumed += value.consumed;
    }
    if (consumed > limits.max_encoded_bytes) return {{qpack_state::limit_exceeded}, 0, {}};
    try {
        if (!name_wire.empty()) instruction.name = qpack_decode_string(name_wire, {limits.max_encoded_bytes, limits.max_name_bytes}, 5, 0x20).value;
        if (!value_wire.empty()) instruction.value = qpack_decode_string(value_wire, {limits.max_encoded_bytes, limits.max_value_bytes}).value;
        return {{}, consumed, std::move(instruction)};
    } catch (const std::bad_alloc&) {
        return {{qpack_state::limit_exceeded}, 0, {}};
    } catch (const std::length_error&) {
        return {{qpack_state::limit_exceeded}, 0, {}};
    }
}
inline qpack_status qpack_apply_encoder_instruction(const qpack_encoder_instruction& instruction, qpack_instruction_limits limits,
                                                   qpack_dynamic_table& table) {
    using kind = qpack_encoder_instruction_kind;
    switch (instruction.kind) {
        case kind::capacity:
            if (instruction.index > SIZE_MAX) return {qpack_state::malformed};
            return table.set_capacity(static_cast<std::size_t>(instruction.index));
        case kind::duplicate: return table.duplicate(instruction.index);
        case kind::insert_literal:
            if (instruction.name.size() > limits.max_name_bytes || instruction.value.size() > limits.max_value_bytes) {
                return {qpack_state::limit_exceeded};
            }
            return table.insert(instruction.name, instruction.value);
        case kind::insert_name: {
            std::string_view name;
            if (instruction.static_name) {
                const auto* entry = qpack_static_lookup(instruction.index);
                if (!entry) return {qpack_state::malformed};
                name = entry->name;
            } else {
                const auto* entry = table.encoder_relative(instruction.index);
                if (!entry) return {qpack_state::malformed};
                name = entry->name;
            }
            if (name.size() > limits.max_name_bytes || instruction.value.size() > limits.max_value_bytes) return {qpack_state::limit_exceeded};
            return table.insert(name, instruction.value);
        }
    }
    return {qpack_state::invalid_argument};
}
inline qpack_encoder_instruction_result qpack_apply_encoder_instruction(std::span<const std::uint8_t> input,
                                                                        qpack_instruction_limits limits, qpack_dynamic_table& table) {
    auto result = qpack_decode_encoder_instruction(input, limits);
    if (!result.status.ok()) return result;
    result.status = qpack_apply_encoder_instruction(result.instruction, limits, table);
    if (!result.status.ok()) result.consumed = 0;
    return result;
}
inline qpack_bytes_result qpack_encode_encoder_instruction(const qpack_encoder_instruction& instruction,
                                                           qpack_instruction_limits limits, bool huffman = false) {
    using kind = qpack_encoder_instruction_kind;
    if (instruction.name.size() > limits.max_name_bytes || instruction.value.size() > limits.max_value_bytes) {
        return {{qpack_state::limit_exceeded}, {}};
    }
    try {
        qpack_bytes_result output;
        switch (instruction.kind) {
            case kind::capacity: output = qpack_encode_integer(instruction.index, 5, 0x20, {}, limits.max_encoded_bytes); break;
            case kind::duplicate: output = qpack_encode_integer(instruction.index, 5, 0, {}, limits.max_encoded_bytes); break;
            case kind::insert_name:
                if (instruction.static_name && !qpack_static_lookup(instruction.index)) return {{qpack_state::invalid_argument}, {}};
                output = qpack_encode_integer(instruction.index, 6, instruction.static_name ? 0xc0 : 0x80, {}, limits.max_encoded_bytes);
                break;
            case kind::insert_literal:
                output = qpack_encode_string(qpack_octets(instruction.name), huffman, {limits.max_encoded_bytes, limits.max_name_bytes},
                                             limits.max_encoded_bytes, 5, 0x20, 0x40);
                break;
            default: return {{qpack_state::invalid_argument}, {}};
        }
        if (!output.status.ok()) return output;
        if (instruction.kind == kind::insert_name || instruction.kind == kind::insert_literal) {
            auto value = qpack_encode_string(qpack_octets(instruction.value), huffman, {limits.max_encoded_bytes, limits.max_value_bytes},
                                             limits.max_encoded_bytes - output.value.size());
            if (!value.status.ok()) return {value.status, {}};
            output.value += value.value;
        }
        output.consumed = output.value.size();
        return output;
    } catch (const std::bad_alloc&) {
        return {{qpack_state::limit_exceeded}, {}};
    } catch (const std::length_error&) {
        return {{qpack_state::limit_exceeded}, {}};
    }
}
enum class qpack_decoder_instruction_kind { section_acknowledgment, stream_cancellation, insert_count_increment };
struct qpack_decoder_instruction {
    qpack_decoder_instruction_kind kind = qpack_decoder_instruction_kind::insert_count_increment;
    std::uint64_t value = 0;
};
struct qpack_decoder_instruction_result {
    qpack_status status;
    std::size_t consumed = 0;
    qpack_decoder_instruction instruction;
};
inline qpack_decoder_instruction_result qpack_decode_decoder_instruction(std::span<const std::uint8_t> input) noexcept {
    if (input.empty()) return {{qpack_state::incomplete}, 0, {}};
    using kind = qpack_decoder_instruction_kind;
    const auto type = (input[0] & 0x80) ? kind::section_acknowledgment :
        ((input[0] & 0x40) ? kind::stream_cancellation : kind::insert_count_increment);
    const auto number = qpack_decode_integer(input, type == kind::section_acknowledgment ? 7 : 6, {});
    if (!number.status.ok()) return {number.status, 0, {}};
    if (type == kind::insert_count_increment && number.value == 0) return {{qpack_state::malformed}, 0, {}};
    return {{}, number.consumed, {type, number.value}};
}
inline qpack_bytes_result qpack_encode_decoder_instruction(qpack_decoder_instruction instruction, std::size_t limit) {
    using kind = qpack_decoder_instruction_kind;
    if (instruction.kind == kind::insert_count_increment && instruction.value == 0) return {{qpack_state::invalid_argument}, {}};
    unsigned prefix;
    std::uint8_t bits;
    switch (instruction.kind) {
        case kind::section_acknowledgment: prefix = 7; bits = 0x80; break;
        case kind::stream_cancellation: prefix = 6; bits = 0x40; break;
        case kind::insert_count_increment: prefix = 6; bits = 0; break;
        default: return {{qpack_state::invalid_argument}, {}};
    }
    try {
        return qpack_encode_integer(instruction.value, prefix, bits, {}, limit);
    } catch (const std::bad_alloc&) {
        return {{qpack_state::limit_exceeded}, {}};
    } catch (const std::length_error&) {
        return {{qpack_state::limit_exceeded}, {}};
    }
}
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_INSTRUCTIONS_HPP_
