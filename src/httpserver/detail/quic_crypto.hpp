/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_crypto.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_CRYPTO_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_CRYPTO_HPP_
#include <array>
#include <httpserver/detail/quic_packet.hpp>
#include <httpserver/detail/tls_psk.hpp>
namespace httpserver::detail {
enum class quic_cipher_suite { aes_128_gcm_sha256, aes_256_gcm_sha384, chacha20_poly1305_sha256 };
enum class quic_key_level { initial, handshake, application };
enum class quic_crypto_code {
    ok, malformed, truncated, no_space, unsupported_suite, authentication_failed,
    provider_failure, keys_unavailable, invalid_key_transition, update_required, limit_reached
};
struct quic_packet_keys {
    quic_cipher_suite suite = quic_cipher_suite::aes_128_gcm_sha256;
    quic_key_level level = quic_key_level::application;
    secure_bytes secret, key, iv, hp;
};
struct quic_initial_keys { quic_packet_keys client, server; };
struct quic_crypto_result {
    quic_crypto_code code = quic_crypto_code::malformed;
    std::size_t consumed = 0, payload_size = 0;
    quic_clear_header header{};
};
using quic_key_observer = secure_bytes::release_observer;
quic_crypto_code derive_quic_initial_keys(std::span<const std::byte> dcid, quic_initial_keys& output,
                                        quic_key_observer observer = nullptr, void* argument = nullptr) noexcept;
quic_crypto_code derive_quic_packet_keys(quic_cipher_suite suite, std::span<const std::byte> secret, quic_packet_keys& output,
                                       quic_key_observer observer = nullptr, void* argument = nullptr) noexcept;
quic_crypto_code derive_quic_next_keys(const quic_packet_keys& current, quic_packet_keys& output,
                                     quic_key_observer observer = nullptr, void* argument = nullptr) noexcept;
quic_crypto_code quic_packet_nonce(const quic_packet_keys& keys, std::uint64_t number, std::span<std::byte> output) noexcept;
quic_crypto_code quic_header_mask(const quic_packet_keys& keys, std::span<const std::byte> sample, std::span<std::byte> output) noexcept;
// All input, output and scratch views must be disjoint. Scratch is cleansed
// after use. Failures leave output unchanged; headers/plaintext publish only
// after authentication. Callers supply padding and short-header CID lengths.
quic_crypto_result protect_quic_packet(const quic_packet_keys& keys, const quic_packet_write& packet,
                                      std::span<std::byte> output, std::span<std::byte> scratch) noexcept;
quic_crypto_result unprotect_quic_packet(const quic_packet_keys& keys, std::span<const std::byte> datagram,
                                        std::optional<std::size_t> short_cid_length, std::optional<std::uint64_t> largest_received,
                                        std::span<std::byte> output, std::span<std::byte> scratch) noexcept;
quic_crypto_code compute_quic_retry_tag(std::span<const std::byte> original_dcid, std::span<const std::byte> retry_without_tag,
                                      std::span<std::byte> output) noexcept;
quic_crypto_code verify_quic_retry_tag(std::span<const std::byte> original_dcid, std::span<const std::byte> retry) noexcept;
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_CRYPTO_HPP_
