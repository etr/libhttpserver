/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_FUZZ_QUIC_EVENT_FUZZ_HPP_
#define TEST_FUZZ_QUIC_EVENT_FUZZ_HPP_
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
namespace quic_fuzz {
inline constexpr std::size_t byte_limit = 4096, event_limit = 64;
struct event { std::uint8_t kind, a, b, c; };
struct report {
    std::array<unsigned, 32> attempted{}, accepted{};
    std::vector<std::uint64_t> trace;
    std::size_t events = 0, consumed = 0, acknowledged = 0, lost = 0, crypto_read = 0, acknowledged_packets = 0;
    unsigned paths_activated = 0, close_notifications = 0, rejected_queue = 0;
    bool released = false;
    bool operator==(const report&) const = default;
};
inline void require(bool value, const char* invariant) {
    if (!value) throw std::logic_error(invariant);
}
template<class Function>
report drive(std::span<const std::uint8_t> input, Function step) {
    report result;
    input = input.first(std::min(input.size(), byte_limit));
    while (result.events < event_limit && input.size() - result.consumed >= 4) {
        const auto at = result.consumed;
        event e{input[at], input[at + 1], input[at + 2], input[at + 3]};
        result.consumed += 4;
        ++result.events;
        step(e, result);
        require(result.trace.size() <= event_limit * 12, "bounded trace");
    }
    return result;
}
inline std::array<std::byte, 16> payload(event e) {
    std::array<std::byte, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = std::byte((e.b + i) % 256);
    return bytes;
}
}  // namespace quic_fuzz
#endif  // TEST_FUZZ_QUIC_EVENT_FUZZ_HPP_
