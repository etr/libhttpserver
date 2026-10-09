/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_FUZZ_QPACK_FUZZ_HPP_
#define TEST_FUZZ_QPACK_FUZZ_HPP_
#include <array>
#include <span>
#include <vector>
#include <httpserver/detail/qpack_field_section.hpp>
struct qpack_fuzz_report {
    httpserver::detail::qpack_state section_state{};
    std::vector<httpserver::detail::qpack_field> fields;
    std::size_t expanded = 0, table_bytes = 0;
    std::uint64_t inserts = 0;
    std::vector<std::uint64_t> trace;
    std::array<unsigned, 32> attempted{}, accepted{};
    bool operator==(const qpack_fuzz_report& other) const;
};
qpack_fuzz_report qpack_fuzz_replay(std::span<const std::uint8_t> input);
std::size_t qpack_fuzz_replay_corpus(const char* directory);
#endif  // TEST_FUZZ_QPACK_FUZZ_HPP_
