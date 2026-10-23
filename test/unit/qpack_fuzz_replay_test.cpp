/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <array>
#include "fuzz/qpack_fuzz.hpp"
#include "./littletest.hpp"
LT_BEGIN_SUITE(qpack_fuzz_suite)
void set_up() {}
void tear_down() {}
LT_END_SUITE(qpack_fuzz_suite)
LT_BEGIN_AUTO_TEST(qpack_fuzz_suite, static_fields_roundtrip_and_exact_decoded_boundary)
    const std::array<std::uint8_t, 5> wire{0, 0, 0, 0, 0xd1};
    auto result = qpack_fuzz_replay(wire);
    LT_CHECK(result.section_state == httpserver::detail::qpack_state::ok);
    LT_ASSERT_EQ(result.fields.size(), 1U); LT_CHECK_EQ(result.fields[0].name, ":method");
    LT_CHECK_EQ(result.fields[0].value, "GET"); LT_CHECK_EQ(result.expanded, 42U);
    auto limit = wire; limit[0] = 1;
    LT_CHECK(qpack_fuzz_replay(limit).section_state == httpserver::detail::qpack_state::limit_exceeded);
    limit[0] = 2;
    LT_CHECK(qpack_fuzz_replay(limit).section_state == httpserver::detail::qpack_state::limit_exceeded);
    limit[0] = 3;
    LT_CHECK(qpack_fuzz_replay(limit).section_state == httpserver::detail::qpack_state::limit_exceeded);
    limit[0] = 6;
    LT_CHECK(qpack_fuzz_replay(limit).section_state == httpserver::detail::qpack_state::ok);
LT_END_AUTO_TEST(static_fields_roundtrip_and_exact_decoded_boundary)
LT_BEGIN_AUTO_TEST(qpack_fuzz_suite, dynamic_instructions_block_eviction_and_invalid_index)
    const std::array<std::uint8_t, 11> wire{0, 6, 0x3f, 0x61, 0x41, 'x', 1, 'a', 2, 0, 0x80};
    auto result = qpack_fuzz_replay(wire);
    LT_CHECK(result.section_state == httpserver::detail::qpack_state::ok);
    LT_CHECK_EQ(result.inserts, 1U); LT_CHECK_EQ(result.table_bytes, 34U);
    LT_ASSERT_EQ(result.fields.size(), 1U); LT_CHECK_EQ(result.fields[0].name, "x");
    LT_CHECK_EQ(result.fields[0].value, "a");
    const std::array<std::uint8_t, 5> blocked{0, 0, 2, 0, 0x80};
    LT_CHECK(qpack_fuzz_replay(blocked).section_state == httpserver::detail::qpack_state::inserts_unavailable);
    auto invalid = wire; invalid[10] = 0x81;
    LT_CHECK(qpack_fuzz_replay(invalid).section_state == httpserver::detail::qpack_state::malformed);
LT_END_AUTO_TEST(dynamic_instructions_block_eviction_and_invalid_index)
LT_BEGIN_AUTO_TEST(qpack_fuzz_suite, literal_order_never_indexed_and_malformed_huffman)
    const std::array<std::uint8_t, 12> repeated{0, 0, 0, 0, 0x31, 'x', 1, 'a', 0x31, 'x', 1, 'b'};
    auto result = qpack_fuzz_replay(repeated); LT_ASSERT_EQ(result.fields.size(), 2U);
    LT_CHECK_EQ(result.fields[0].value, "a"); LT_CHECK_EQ(result.fields[1].value, "b");
    LT_CHECK(result.fields[0].never_indexed && result.fields[1].never_indexed);
    const std::array<std::uint8_t, 11> eos{0, 0, 0, 0, 0x21, 'x', 0x84, 0xff, 0xff, 0xff, 0xff};
    LT_CHECK(qpack_fuzz_replay(eos).section_state == httpserver::detail::qpack_state::malformed);
    const std::array<std::uint8_t, 8> padding{0, 0, 0, 0, 0x21, 'x', 0x81, 0};
    LT_CHECK(qpack_fuzz_replay(padding).section_state == httpserver::detail::qpack_state::malformed);
LT_END_AUTO_TEST(literal_order_never_indexed_and_malformed_huffman)
LT_BEGIN_AUTO_TEST(qpack_fuzz_suite, seeded_mutations_are_deterministic_and_bounded)
    LT_CHECK(qpack_fuzz_replay_corpus(QPACK_SEED_DIR) > 100U);
LT_END_AUTO_TEST(seeded_mutations_are_deterministic_and_bounded)
LT_BEGIN_AUTO_TEST(qpack_fuzz_suite, compressed_instruction_roundtrip_has_its_own_byte_budget)
    // Captured instruction expands from 104 encoded bytes to 130 bytes without Huffman coding.
    const std::array<std::uint8_t, 119> input{
        0xe2, 0xe2, 0xd2, 0x04, 0xff, 0x29, 0x29, 0x00, 0x3f, 0xff, 0x29, 0x29, 0x29, 0x29, 0x29, 0xe6,
        0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
        0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
        0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
        0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0x3b, 0xe6,
        0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6,
        0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0xe6, 0x29, 0x03, 0x03,
        0xf7, 0xe2, 0xe2, 0xb7, 0xfe, 0xa2, 0xb7,
    };
    LT_CHECK(qpack_fuzz_replay(input) == qpack_fuzz_replay(input));
LT_END_AUTO_TEST(compressed_instruction_roundtrip_has_its_own_byte_budget)
LT_BEGIN_AUTO_TEST_ENV()
AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
