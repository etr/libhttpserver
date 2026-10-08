/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_address_token.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_ADDRESS_TOKEN_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_ADDRESS_TOKEN_HPP_
#include <vector>
#include <httpserver/detail/quic_amplification.hpp>
#include <httpserver/detail/quic_invariant_header.hpp>
#include <httpserver/detail/tls_psk.hpp>
namespace httpserver::detail {
struct quic_token_claims { quic_cid original_destination, retry_source; };
class quic_address_token final {
 public:
    static constexpr std::size_t maximum_size = 84;
    static constexpr std::uint64_t lifetime_seconds = 10;
    // An empty key uses private provider randomness. Explicit keys are for
    // reproducible tests or listener-owned key provisioning, never Retry's public key.
    explicit quic_address_token(std::span<const std::byte> key = {});
    std::optional<std::vector<std::byte>> issue(const quic_token_claims& claims,
        const quic_path_identity& path, std::uint64_t listener, std::uint64_t now) const;
    std::optional<quic_token_claims> verify(std::span<const std::byte> token,
        const quic_path_identity& path, std::uint64_t listener, const quic_cid& retry, std::uint64_t now) const;

 private:
    bool mac(std::span<const std::byte> payload, const quic_path_identity& path,
        std::uint64_t listener, std::span<std::byte> output) const;
    secure_bytes secret_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_ADDRESS_TOKEN_HPP_
