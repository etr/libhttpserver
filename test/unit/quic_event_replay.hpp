/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_QUIC_EVENT_REPLAY_HPP_
#define TEST_UNIT_QUIC_EVENT_REPLAY_HPP_
#include <algorithm>
#include <filesystem>  // NOLINT(build/c++17): the native target requires C++20.
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include "fuzz/quic_event_fuzz.hpp"
namespace quic_fuzz {
template<class Replay>
std::size_t replay_corpus(const char* directory, Replay replay) {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.path().extension() == ".seed") paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    require(!paths.empty(), "nonempty corpus");
    std::uint32_t random = 176;
    std::size_t count = 0;
    std::array<unsigned, 32> attempted{}, accepted{};
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary);
        require(file.good(), "read corpus");
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
        auto result = replay(bytes);
        require(result == replay(bytes), "deterministic authored replay");
        for (unsigned i = 0; i < 32; ++i) {
            attempted[i] += result.attempted[i];
            accepted[i] += result.accepted[i];
        }
        ++count;
        for (unsigned i = 0; i < 32; ++i) {
            auto mutation = bytes;
            random = random * 1664525U + 1013904223U;
            if (!mutation.empty()) mutation[random % mutation.size()] ^= static_cast<std::uint8_t>(random >> 24);
            require(replay(mutation) == replay(mutation), "deterministic mutated replay");
            replay(std::span(bytes).first(bytes.empty() ? 0 : i % bytes.size()));
            count += 2;
        }
    }
    std::cout << "replays=" << count << " attempted/accepted=";
    for (unsigned i = 0; i < 32; ++i)
        if (attempted[i]) std::cout << i << ':' << attempted[i] << '/' << accepted[i] << ' ';
    std::cout << '\n';
    return count;
}
}  // namespace quic_fuzz
#endif  // TEST_UNIT_QUIC_EVENT_REPLAY_HPP_
