/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <cstdint>
#include <filesystem>  // NOLINT(build/c++17): the native target requires C++20.
#include <fstream>
#include <iterator>
#include <vector>
#include "../fuzz/quic_parser_fuzz.hpp"
#include "../fuzz/quic_state_fuzz.hpp"
#include "./littletest.hpp"
LT_BEGIN_SUITE(quic_fuzz_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(quic_fuzz_suite)
LT_BEGIN_AUTO_TEST(quic_fuzz_suite, parser_modes_use_real_invariant_header)
    const std::vector<std::uint8_t> short_header{0x40, 1, 0, 9};
    LT_CHECK_EQ(quic_parser_fuzz_input(short_header), std::size_t{2});
    const std::vector<std::uint8_t> long_header{0xc0, 0, 0, 0, 1, 2, 1, 0, 0};
    LT_CHECK_EQ(quic_parser_fuzz_input(long_header), std::size_t{5});
    LT_CHECK_EQ(quic_parser_fuzz_input({}), std::size_t{0});
LT_END_AUTO_TEST(parser_modes_use_real_invariant_header)
LT_BEGIN_AUTO_TEST(quic_fuzz_suite, state_actions_produce_a_replay_trace)
    // register 1, send four bytes, duplicate pending slot 0, deliver slot 1,
    // drain, retire 1, deliver retired slot 0, timer, advance, teardown.
    const std::vector<std::uint8_t> script{0, 1, 1, 4, 0, 0x40, 1, 0, 9, 4, 0, 3, 1, 6, 9, 1, 3, 0, 7, 5, 5, 5, 11};
    auto trace = quic_state_fuzz_input(script);
    LT_ASSERT(trace.size() > 3);
    LT_CHECK_EQ(trace[0], std::uint8_t{'Q'});
    LT_CHECK(trace == quic_state_fuzz_input(script));
LT_END_AUTO_TEST(state_actions_produce_a_replay_trace)
LT_BEGIN_AUTO_TEST(quic_fuzz_suite, committed_seeds_and_fixed_seed_mutations)
    std::uint32_t random = 150;
    for (const char* target : {"parser", "state"}) {
        std::vector<std::filesystem::path> seeds;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(QUIC_SEED_DIR) / target)) {
            if (entry.path().extension() == ".seed") seeds.push_back(entry.path());
        }
        std::sort(seeds.begin(), seeds.end());
        LT_ASSERT(!seeds.empty());
        for (const auto& path : seeds) {
            std::ifstream input(path, std::ios::binary);
            LT_ASSERT(input.good());
            std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
            auto replay = [&](const auto& value) {
                if (target[0] == 'p') quic_parser_fuzz_input(value);
                else LT_CHECK(quic_state_fuzz_input(value).size() >= 3);
            };
            replay(bytes);
            for (unsigned i = 0; i < 32; ++i) {
                auto mutation = bytes;
                random = random * 1664525U + 1013904223U;
                if (!mutation.empty()) mutation[random % mutation.size()] ^= static_cast<std::uint8_t>(random >> 24);
                replay(mutation);
                replay(std::span(bytes).first(bytes.empty() ? 0 : i % bytes.size()));
            }
        }
    }
LT_END_AUTO_TEST(committed_seeds_and_fixed_seed_mutations)
LT_BEGIN_AUTO_TEST(quic_fuzz_suite, codec_modes_reach_the_real_packet_frame_and_parameter_decoders)
    const std::vector<std::uint8_t> ping{1}, parameters{15, 0};
    auto packet = std::vector<std::uint8_t>{0xc0, 0, 0, 0, 1, 0, 0, 0, 17, 0}; packet.resize(26);
    LT_CHECK_EQ(quic_codec_fuzz_input(ping), std::uint32_t{2});
    LT_CHECK_EQ(quic_codec_fuzz_input(parameters), std::uint32_t{4});
    LT_CHECK((quic_codec_fuzz_input(packet) & 1) != 0);
    const std::vector<std::uint8_t> nonminimal_ping{0x40, 1};
    LT_CHECK((quic_codec_fuzz_input(nonminimal_ping) & 2) == 0);
    const std::vector<std::uint8_t> duplicate_parameters{27, 0, 27, 0};
    LT_CHECK((quic_codec_fuzz_input(duplicate_parameters) & 4) == 0);
LT_END_AUTO_TEST(codec_modes_reach_the_real_packet_frame_and_parameter_decoders)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
