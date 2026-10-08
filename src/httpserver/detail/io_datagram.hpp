/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "io_datagram.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_IO_DATAGRAM_HPP_
#define SRC_HTTPSERVER_DETAIL_IO_DATAGRAM_HPP_
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <httpserver/net/address.hpp>
namespace httpserver {
namespace detail {
constexpr std::size_t k_max_datagram_bytes = 65507;
constexpr std::size_t k_udp_pending_operations = 64;
constexpr std::size_t k_udp_pending_bytes = 1024 * 1024;
// Scope is separate from the normalized address (notably for IPv6 link-local).
struct datagram_endpoint {
    net::peer_address peer;
    std::uint32_t scope = 0;
    bool operator==(const datagram_endpoint&) const = default;
};
struct io_datagram {
    std::vector<std::byte> bytes;
    datagram_endpoint peer;
    std::optional<datagram_endpoint> local;
    std::optional<std::uint32_t> interface_index;
    std::uint64_t socket_id = 0;
    std::chrono::steady_clock::time_point received_at{};
};
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_IO_DATAGRAM_HPP_
