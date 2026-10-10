/*
 * Copyright (C) 2026 Sebastiano Merlino
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef TEST_INTEG_NONCE_EXPIRY_WAIT_HPP_
#define TEST_INTEG_NONCE_EXPIRY_WAIT_HPP_

#include <charconv>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace httpserver_test {
// The native nonce begins with its wire-format epoch-seconds timestamp.
// Observe expiry under a monotonic budget: a fixed sleep cannot prove that the
// server's wall clock advanced, especially when VM clock synchronization steps it.
template<class Wall, class Pause>
bool wait_for_nonce_expiry(std::string_view nonce, Wall wall, Pause pause,
                           std::chrono::steady_clock::duration budget) {
    std::uint64_t stamp = 0;
    if (nonce.size() < 16) throw std::invalid_argument("short nonce timestamp");
    const auto parsed = std::from_chars(nonce.data(), nonce.data() + 16, stamp, 16);
    if (parsed.ec != std::errc() || parsed.ptr != nonce.data() + 16) throw std::invalid_argument("invalid nonce timestamp");
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (wall() > stamp) return true;
        pause();
    }
    return false;
}
}  // namespace httpserver_test
#endif  // TEST_INTEG_NONCE_EXPIRY_WAIT_HPP_
