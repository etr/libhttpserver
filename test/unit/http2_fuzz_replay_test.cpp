/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "../conformance/corpus.hpp"
#include "../fuzz/http2_engine_fuzz.hpp"
#include "./littletest.hpp"
LT_BEGIN_SUITE(http2_fuzz_replay_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(http2_fuzz_replay_suite)
LT_BEGIN_AUTO_TEST(http2_fuzz_replay_suite, bounded_engine_seed_mutations)
    std::uint32_t random = 145;
    for (const auto& entry : protocol_corpus::load(HTTP2_ENGINE_CORPUS_DIR)) {
        std::vector<std::uint8_t> bytes(entry.wire.begin(), entry.wire.end());
        http2_engine_fuzz_input(bytes);
        for (unsigned i = 0; i < 32; ++i) {
            auto mutation = bytes;
            random = random * 1664525U + 1013904223U;
            mutation[random % mutation.size()] ^= static_cast<std::uint8_t>(random >> 24);
            http2_engine_fuzz_input(mutation);
            http2_engine_fuzz_input(std::span(bytes).first(i % bytes.size()));
        }
    }
    LT_CHECK(true);
LT_END_AUTO_TEST(bounded_engine_seed_mutations)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
