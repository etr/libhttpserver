/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef TEST_UNIT_HTTP3_FIXTURE_HPP_
#define TEST_UNIT_HTTP3_FIXTURE_HPP_
#include <vector>
#include <httpserver/detail/http3_connection.hpp>
namespace h3test {
namespace hd = httpserver::detail;
namespace hs = httpserver::server;
inline hs::resource_budget budget() { return hs::resource_budget::root(hs::budget_limits{}); }
inline std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (auto v : values) result.push_back(static_cast<std::byte>(v));
    return result;
}
inline void integer(std::vector<std::byte>& wire, std::uint64_t value, std::size_t width = 0) {
    std::array<std::byte, 8> out{};
    const auto r = hd::encode_quic_varint(value, out, width);
    wire.insert(wire.end(), out.begin(), out.begin() + r.consumed);
}
inline std::vector<std::byte> frame(std::uint64_t type, std::span<const std::byte> payload, std::size_t width = 0) {
    std::vector<std::byte> out;
    integer(out, type, width); integer(out, payload.size(), width);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
inline hd::http3_feed_result feed(hd::http3_connection& c, std::uint64_t id, std::span<const std::byte> wire) {
    return c.feed(id, wire, c.offset(id));
}
}  // namespace h3test
#endif  // TEST_UNIT_HTTP3_FIXTURE_HPP_
