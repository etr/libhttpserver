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
qpack_encoder_instruction_result qpack_decode_encoder_instruction_allocated(std::span<const std::uint8_t> input,
                                                                         qpack_instruction_limits limits);
qpack_encoder_instruction_result qpack_decode_encoder_instruction(std::span<const std::uint8_t> input,
                                                                         qpack_instruction_limits limits);
qpack_status qpack_apply_encoder_instruction_allocated(const qpack_encoder_instruction& instruction, qpack_instruction_limits limits,
                                                   qpack_dynamic_table& table);
qpack_status qpack_apply_encoder_instruction(const qpack_encoder_instruction& instruction, qpack_instruction_limits limits,
                                                   qpack_dynamic_table& table);
qpack_encoder_instruction_result qpack_apply_encoder_instruction(std::span<const std::uint8_t> input,
                                                                        qpack_instruction_limits limits, qpack_dynamic_table& table);
qpack_bytes_result qpack_encode_encoder_instruction(const qpack_encoder_instruction& instruction,
                                                           qpack_instruction_limits limits, bool huffman = false);
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
qpack_decoder_instruction_result qpack_decode_decoder_instruction(std::span<const std::uint8_t> input) noexcept;
qpack_bytes_result qpack_encode_decoder_instruction(qpack_decoder_instruction instruction, std::size_t limit);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_INSTRUCTIONS_HPP_
