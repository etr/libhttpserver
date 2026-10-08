/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_DETAIL_QUIC_CRYPTO_PROVIDER_HPP_
#define SRC_DETAIL_QUIC_CRYPTO_PROVIDER_HPP_
#include <openssl/evp.h>
#include <memory>
#include <httpserver/detail/quic_crypto.hpp>
#include <httpserver/detail/secure_zero.hpp>
namespace httpserver::detail {
struct quic_suite_spec { const char* digest; const char* aead; const char* protection; std::size_t secret_size, key_size; };
inline const quic_suite_spec* quic_suite(quic_cipher_suite suite) noexcept {
    static const std::array<quic_suite_spec, 3> specs{{
        {"SHA256", "AES-128-GCM", "AES-128-ECB", 32, 16},
        {"SHA384", "AES-256-GCM", "AES-256-ECB", 48, 32},
        {"SHA256", "CHACHA20-POLY1305", "CHACHA20", 32, 32}}};
    auto index = static_cast<std::size_t>(suite);
    return index < specs.size() ? &specs[index] : nullptr;
}
template <std::size_t N>
struct quic_temporary {
    std::array<std::byte, N> bytes{};
    ~quic_temporary() { secure_zero(bytes.data(), bytes.size()); }
};
struct quic_scratch_guard {
    std::span<std::byte> bytes;
    ~quic_scratch_guard() { secure_zero(bytes.data(), bytes.size()); }
};
using quic_cipher = std::unique_ptr<EVP_CIPHER, decltype(&EVP_CIPHER_free)>;
using quic_cipher_context = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
inline const unsigned char* quic_data(std::span<const std::byte> bytes) { return reinterpret_cast<const unsigned char*>(bytes.data()); }
inline unsigned char* quic_data(std::span<std::byte> bytes) { return reinterpret_cast<unsigned char*>(bytes.data()); }
template <std::size_t N>
inline unsigned char* quic_data(std::array<std::byte, N>& bytes) { return quic_data(std::span<std::byte>(bytes)); }
inline bool quic_overlap(std::span<const std::byte> a, std::span<const std::byte> b) noexcept {
    if (a.empty() || b.empty()) return false;
    auto x = reinterpret_cast<std::uintptr_t>(a.data()), y = reinterpret_cast<std::uintptr_t>(b.data());
    return x <= y ? y - x < a.size() : x - y < b.size();
}
inline bool quic_keys_valid(const quic_packet_keys& keys) noexcept {
    auto spec = quic_suite(keys.suite);
    return spec && keys.key.bytes().size() == spec->key_size && keys.hp.bytes().size() == spec->key_size && keys.iv.bytes().size() == 12;
}
inline bool quic_level_matches(const quic_packet_keys& keys, quic_packet_kind kind) noexcept {
    if (kind == quic_packet_kind::initial) return keys.level == quic_key_level::initial;
    if (kind == quic_packet_kind::handshake) return keys.level == quic_key_level::handshake;
    return kind == quic_packet_kind::one_rtt && keys.level == quic_key_level::application;
}
struct quic_authenticated_check {
    quic_crypto_code (*function)(const quic_clear_header&, void*) noexcept = nullptr;
    void* argument = nullptr;
};
// Apply connection-owned transition checks after authentication, before any
// plaintext is committed to output. The callback cannot retain borrowed views.
quic_crypto_result quic_open_checked(const quic_packet_keys& keys, std::span<const std::byte> datagram,
                                     std::optional<std::size_t> cid_length, std::optional<std::uint64_t> largest,
                                     std::span<std::byte> output, std::span<std::byte> scratch, quic_authenticated_check check) noexcept;
}  // namespace httpserver::detail
#endif  // SRC_DETAIL_QUIC_CRYPTO_PROVIDER_HPP_
