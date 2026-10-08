/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_FUZZ_QUIC_STATE_FUZZ_HPP_
#define TEST_FUZZ_QUIC_STATE_FUZZ_HPP_
#include <cstdint>
#include <span>
#include <vector>
std::vector<std::uint8_t> quic_state_fuzz_input(std::span<const std::uint8_t> bytes);
#endif  // TEST_FUZZ_QUIC_STATE_FUZZ_HPP_
