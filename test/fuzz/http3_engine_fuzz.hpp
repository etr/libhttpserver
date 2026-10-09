/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_FUZZ_HTTP3_ENGINE_FUZZ_HPP_
#define TEST_FUZZ_HTTP3_ENGINE_FUZZ_HPP_
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
struct http3_fuzz_observation {
    std::uint64_t stream = 0, type = 0, begin = 0;
    std::string value;
    bool operator==(const http3_fuzz_observation&) const = default;
};
struct http3_fuzz_report {
    std::uint64_t error = 0;
    bool settings = false;
    std::size_t blocked_final = 0;
    std::vector<http3_fuzz_observation> observations;
    std::array<std::string, 3> output;
    std::array<unsigned, 32> attempted{}, accepted{};
    bool operator==(const http3_fuzz_report&) const = default;
};
http3_fuzz_report http3_fuzz_replay(std::span<const std::uint8_t> input);
std::size_t http3_fuzz_replay_corpus(const char* directory);
#endif  // TEST_FUZZ_HTTP3_ENGINE_FUZZ_HPP_
