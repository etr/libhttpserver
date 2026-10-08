/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_FUZZ_QUIC_PARSER_FUZZ_HPP_
#define TEST_FUZZ_QUIC_PARSER_FUZZ_HPP_
#include <cstddef>
#include <cstdint>
#include <span>
// Success bits: envelope=1, complete frame payload=2, client parameters=4.
std::uint32_t quic_codec_fuzz_input(std::span<const std::uint8_t> bytes);
std::size_t quic_parser_fuzz_input(std::span<const std::uint8_t> bytes);
#endif  // TEST_FUZZ_QUIC_PARSER_FUZZ_HPP_
