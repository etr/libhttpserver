/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <httpserver/detail/quic_invariant_header.hpp>
#include "fuzz/quic_parser_fuzz.hpp"
namespace {
namespace hd = httpserver::detail;
void require(bool value) { if (!value) std::abort(); }
}  // namespace
std::size_t quic_parser_fuzz_input(std::span<const std::uint8_t> input) {
    input = input.first(std::min<std::size_t>(input.size(), 4096));
    const auto bytes = std::as_bytes(input);
    const std::array<std::optional<std::size_t>, 5> modes{std::nullopt, 0, 2, 20, 21};
    std::size_t successes = 0;
    for (auto length : modes) {
        const auto first = hd::extract_quic_invariant_header(bytes, length);
        const auto second = hd::extract_quic_invariant_header(bytes, length);
        require(first.has_value() == second.has_value());
        if (!first) continue;
        ++successes;
        require(first->destination.size <= 20 && first->source.size <= 20);
        require(first->destination == second->destination && first->source == second->source);
        require(first->version == second->version && first->long_header == second->long_header);
        require(!input.empty() && first->long_header == ((input[0] & 0x80) != 0));
        std::size_t destination_offset = 1;
        if (first->long_header) {
            require(input.size() >= 7);
            const auto version = (static_cast<std::uint32_t>(input[1]) << 24) | (static_cast<std::uint32_t>(input[2]) << 16) | (static_cast<std::uint32_t>(input[3]) << 8) | input[4];
            require(first->version == version && first->destination.size == input[5]);
            destination_offset = 6;
            const auto source_length_offset = destination_offset + first->destination.size;
            require(source_length_offset < input.size() && first->source.size == input[source_length_offset]);
            require(first->source.size <= input.size() - source_length_offset - 1);
            for (std::size_t i = 0; i < first->source.size; ++i) require(first->source.bytes[i] == bytes[source_length_offset + 1 + i]);
        } else {
            require(length && *length <= 20 && first->destination.size == *length);
            require(first->source.size == 0 && first->version == 0);
        }
        require(first->destination.size <= input.size() - destination_offset);
        for (std::size_t i = 0; i < first->destination.size; ++i) require(first->destination.bytes[i] == bytes[destination_offset + i]);
    }
    return successes;
}
#ifdef QUIC_PARSER_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* bytes, std::size_t size) {
    quic_parser_fuzz_input({bytes, size}); return 0;
}
#endif
