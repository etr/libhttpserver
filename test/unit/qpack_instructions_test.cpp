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

#include <string>
#include <utility>
#include <httpserver/detail/qpack_instructions.hpp>
#include "./littletest.hpp"
using httpserver::detail::qpack_instruction_limits;
using httpserver::detail::qpack_encoder_instruction_kind;
using httpserver::detail::qpack_encoder_instruction;
using httpserver::detail::qpack_dynamic_table;
using httpserver::detail::qpack_table_role;
using httpserver::detail::qpack_state;
using httpserver::detail::qpack_octets;
using httpserver::detail::qpack_encode_encoder_instruction;
using httpserver::detail::qpack_apply_encoder_instruction;
using httpserver::detail::qpack_decode_encoder_instruction;
using httpserver::detail::qpack_decoder_instruction_kind;
using httpserver::detail::qpack_decoder_instruction;
using httpserver::detail::qpack_encode_decoder_instruction;
using httpserver::detail::qpack_decode_decoder_instruction;
namespace {
constexpr qpack_instruction_limits limits{4096, 1024, 1024};
std::string unhex(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        out += static_cast<char>((digit(text[i]) << 4) | digit(text[i + 1]));
    }
    return out;
}
}  // namespace
LT_BEGIN_SUITE(instruction_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(instruction_suite)
LT_BEGIN_AUTO_TEST(instruction_suite, independent_encoder_wire_and_atomic_fragmentation)
    using kind = qpack_encoder_instruction_kind;
    const qpack_encoder_instruction instructions[] = {{kind::capacity, 220, false, "", ""}, {kind::insert_name, 0, true, "", "www.example.com"},
        {kind::insert_name, 1, true, "", "/sample/path"}, {kind::insert_literal, 0, false, "custom-key", "custom-value"},
        {kind::duplicate, 2, false, "", ""}, {kind::insert_name, 1, false, "", "custom-value2"}};
    const std::string_view fixtures[] = {"3fbd01", "c00f7777772e6578616d706c652e636f6d", "c10c2f73616d706c652f70617468",
        "4a637573746f6d2d6b65790c637573746f6d2d76616c7565", "02", "810d637573746f6d2d76616c756532"};
    auto budget = httpserver::server::resource_budget::root({});
    qpack_dynamic_table t(qpack_table_role::decoder);
    LT_CHECK(t.initialize(budget, 220).ok());
    for (std::size_t i = 0; i < 6; ++i) {
        const auto wire = unhex(fixtures[i]);
        LT_CHECK(qpack_encode_encoder_instruction(instructions[i], limits).value == wire);
        for (std::size_t n = 0; n < wire.size(); ++n) {
            const auto before = t.insert_count();
            const auto fragmented = qpack_apply_encoder_instruction(qpack_octets(wire).first(n), limits, t);
            LT_CHECK(fragmented.status.state == qpack_state::incomplete && fragmented.consumed == 0);
            LT_CHECK(t.insert_count() == before);
        }
        const auto parsed = qpack_decode_encoder_instruction(qpack_octets(wire + "\x20"), limits);
        LT_CHECK(parsed.status.ok() && parsed.consumed == wire.size() && parsed.instruction.kind == instructions[i].kind);
        LT_CHECK(qpack_apply_encoder_instruction(qpack_octets(wire), limits, t).status.ok());
    }
    LT_CHECK(t.bytes() == 215 && t.size() == 4 && t.insert_count() == 5 && !t.lookup(0));
LT_END_AUTO_TEST(independent_encoder_wire_and_atomic_fragmentation)
LT_BEGIN_AUTO_TEST(instruction_suite, huffman_limits_and_invalid_indexes)
    using kind = qpack_encoder_instruction_kind;
    qpack_encoder_instruction custom{kind::insert_literal, 0, false, "custom-key", "custom-value"};
    const auto expected = unhex("6825a849e95ba97d7f8925a849e95bb8e8b4bf");
    LT_CHECK(qpack_encode_encoder_instruction(custom, limits, true).value == expected);
    const auto decoded = qpack_decode_encoder_instruction(qpack_octets(expected), limits);
    LT_CHECK(decoded.status.ok() && decoded.instruction.name == "custom-key" && decoded.instruction.value == "custom-value");
    LT_CHECK(qpack_encode_encoder_instruction(custom, {1, 1024, 1024}).value.empty());
    for (auto hex : {"68ffffffffffffffff00", "610000", "c002ff"}) {
        LT_CHECK(!qpack_decode_encoder_instruction(qpack_octets(unhex(hex)), limits).status.ok());
    }
    auto budget = httpserver::server::resource_budget::root({});
    qpack_dynamic_table t(qpack_table_role::decoder);
    LT_CHECK(t.initialize(budget, 64).ok());
    for (auto hex : {"3f2200", "ff2400", "8000", "00"}) {
        const auto result = qpack_apply_encoder_instruction(qpack_octets(unhex(hex)), limits, t);
        LT_CHECK(result.status.state == qpack_state::malformed && result.consumed == 0 && t.insert_count() == 0);
    }
    LT_CHECK(qpack_decode_encoder_instruction(qpack_octets(expected), {4096, 9, 1024}).status.state == qpack_state::limit_exceeded);
LT_END_AUTO_TEST(huffman_limits_and_invalid_indexes)
LT_BEGIN_AUTO_TEST(instruction_suite, independent_feedback_discriminators_and_prefixes)
    using kind = qpack_decoder_instruction_kind;
    const qpack_decoder_instruction instructions[] = {{kind::section_acknowledgment, 4}, {kind::stream_cancellation, 8},
        {kind::insert_count_increment, 1}, {kind::section_acknowledgment, 127}, {kind::stream_cancellation, 64}, {kind::insert_count_increment, 63}};
    const std::string_view fixtures[] = {"84", "48", "01", "ff00", "7f01", "3f00"};
    for (std::size_t i = 0; i < 6; ++i) {
        const auto wire = unhex(fixtures[i]);
        LT_CHECK(qpack_encode_decoder_instruction(instructions[i], 16).value == wire);
        const auto parsed = qpack_decode_decoder_instruction(qpack_octets(wire + "\x01"));
        LT_CHECK(parsed.status.ok() && parsed.consumed == wire.size());
        LT_CHECK(parsed.instruction.kind == instructions[i].kind && parsed.instruction.value == instructions[i].value);
        for (std::size_t n = 0; n < wire.size(); ++n) {
            const auto partial = qpack_decode_decoder_instruction(qpack_octets(wire).first(n));
            LT_CHECK(partial.status.state == qpack_state::incomplete && partial.consumed == 0);
        }
    }
    LT_CHECK(qpack_decode_decoder_instruction(qpack_octets(unhex("00"))).status.state == qpack_state::malformed);
    LT_CHECK(qpack_encode_decoder_instruction({kind::insert_count_increment, 0}, 16).status.state == qpack_state::invalid_argument);
LT_END_AUTO_TEST(independent_feedback_discriminators_and_prefixes)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
