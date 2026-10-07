/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#ifndef TEST_UNIT_HTTP2_FIXTURE_HPP_
#define TEST_UNIT_HTTP2_FIXTURE_HPP_
#include <algorithm>
#include <string>
#include <vector>
#include <httpserver/detail/http2_frame.hpp>
namespace h2test {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
inline hs::resource_budget budget() { return hs::resource_budget::root({}); }
inline std::vector<std::uint8_t> frame(std::uint8_t type, std::uint8_t flags = 0,
        std::uint32_t stream = 0, std::vector<std::uint8_t> payload = {}) {
    auto n = payload.size();
    std::vector<std::uint8_t> out{static_cast<std::uint8_t>(n >> 16), static_cast<std::uint8_t>(n >> 8),
        static_cast<std::uint8_t>(n), type, flags, static_cast<std::uint8_t>(stream >> 24),
        static_cast<std::uint8_t>(stream >> 16), static_cast<std::uint8_t>(stream >> 8), static_cast<std::uint8_t>(stream)};
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
inline std::vector<std::uint8_t> preface() {
    std::vector<std::uint8_t> out(hd::http2_magic.begin(), hd::http2_magic.end());
    const auto settings = frame(4);
    out.insert(out.end(), settings.begin(), settings.end());
    return out;
}
inline void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& in) { out.insert(out.end(), in.begin(), in.end()); }
inline std::vector<std::uint8_t> setting(std::uint16_t id, std::uint32_t value) {
    return {static_cast<std::uint8_t>(id >> 8), static_cast<std::uint8_t>(id), static_cast<std::uint8_t>(value >> 24),
        static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
}
}  // namespace h2test
#endif  // TEST_UNIT_HTTP2_FIXTURE_HPP_
