/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_QUIC_CODEC_TEST_SUPPORT_HPP_
#define TEST_UNIT_QUIC_CODEC_TEST_SUPPORT_HPP_
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <initializer_list>
#include <vector>
#include "./littletest.hpp"
namespace hd = httpserver::detail;
inline std::vector<std::byte> octets(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (auto value : values)
        result.push_back(std::byte(value));
    return result;
}
#endif  // TEST_UNIT_QUIC_CODEC_TEST_SUPPORT_HPP_
