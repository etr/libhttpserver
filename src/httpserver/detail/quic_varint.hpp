/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_varint.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_VARINT_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_VARINT_HPP_
#include <optional>
#include <httpserver/detail/quic_codec.hpp>
namespace httpserver {
namespace detail {
std::size_t quic_varint_width(std::uint64_t value) noexcept;
quic_decode_result<std::uint64_t> decode_quic_varint(std::span<const std::byte> bytes) noexcept;
quic_encode_result encode_quic_varint(std::uint64_t value, std::span<std::byte> output, std::size_t width = 0) noexcept;
quic_encode_result encode_quic_packet_number(std::uint64_t full, std::size_t width, std::span<std::byte> output) noexcept;
quic_decode_result<std::uint64_t> reconstruct_quic_packet_number(std::uint64_t truncated, std::size_t width, std::optional<std::uint64_t> largest_received) noexcept;
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_VARINT_HPP_
