/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_invariant_header.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_INVARIANT_HEADER_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_INVARIANT_HEADER_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace httpserver {
namespace detail {
constexpr std::size_t k_quic_cid_bytes = 20;
struct quic_cid {
    std::array<std::byte, k_quic_cid_bytes> bytes{};
    std::size_t size = 0;
    bool operator==(const quic_cid& other) const noexcept;
};
struct quic_invariant_header {
    quic_cid destination;
    quic_cid source;
    std::uint32_t version = 0;
    bool long_header = false;
};
// QUIC-v1/shared-listener contract: CIDs are at most 20 bytes. Long
// headers expose version and length-prefixed CIDs (including version 0).
// Short headers require a configured CID length, never protected-byte guesses.
// This extracts routing metadata only, without splitting coalesced packets or
// validating packet numbers, crypto payload, or version-specific semantics.
std::optional<quic_invariant_header> extract_quic_invariant_header(
    std::span<const std::byte> bytes, std::optional<std::size_t> short_cid_length = std::nullopt);
}  // namespace detail
}  // namespace httpserver
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_INVARIANT_HEADER_HPP_
