/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_key_state.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_KEY_STATE_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_KEY_STATE_HPP_
#include <optional>
#include <httpserver/detail/quic_crypto.hpp>
namespace httpserver::detail {
enum class quic_key_direction { read, write };
// Smaller limits support bounded policies and near-limit tests; larger limits
// are clamped to RFC 9001. Authentication failures count across all generations.
struct quic_usage_limits {
    std::uint64_t encrypted_packets = k_quic_max_integer;
    std::uint64_t authentication_failures = std::uint64_t{1} << 52;
};
// Serialized owner-thread object. The caller owns TLS handshake/ACK/PTO facts,
// packet-number allocation and scheduling. Retirement is explicitly caller-driven.
class quic_key_state final {
 public:
    explicit quic_key_state(quic_usage_limits limits = {}, quic_key_observer observer = nullptr, void* argument = nullptr) noexcept;
    quic_crypto_code install_initial(quic_endpoint_role role, std::span<const std::byte> dcid) noexcept;
    quic_crypto_code install_traffic_secret(quic_key_level level, quic_key_direction direction, quic_cipher_suite suite, std::span<const std::byte> secret) noexcept;
    quic_crypto_code prepare_next_application_keys(quic_key_direction direction) noexcept;
    quic_crypto_code advance_write_keys(bool handshake_confirmed, bool current_generation_acknowledged) noexcept;
    quic_crypto_code respond_to_peer_update(bool handshake_confirmed) noexcept;
    quic_crypto_result protect_packet(quic_key_level level, quic_packet_write packet, std::span<std::byte> output, std::span<std::byte> scratch) noexcept;
    quic_crypto_result open_packet(quic_key_level level, std::span<const std::byte> datagram, std::optional<std::size_t> cid_length,
                                  std::optional<std::uint64_t> largest, bool handshake_complete, std::span<std::byte> output, std::span<std::byte> scratch) noexcept;
    void retire_previous_read_keys() noexcept;
    void discard_level(quic_key_level level) noexcept;
    void clear() noexcept;
    const quic_packet_keys* keys(quic_key_level level, quic_key_direction direction) const noexcept;
    std::uint64_t generation(quic_key_direction direction) const noexcept;
    std::uint64_t authentication_failures() const noexcept { return failures_; }

 private:
    struct level_state {
        std::optional<quic_packet_keys> read, write;
        std::optional<std::uint64_t> largest_sent;
        std::uint64_t encrypted = 0;
        bool discarded = false;
    };
    struct application_state {
        std::optional<quic_packet_keys> next_read, previous_read, next_write, dummy_read;
        std::optional<std::uint64_t> minimum_received, maximum_received, previous_maximum;
        std::uint64_t read_generation = 0, write_generation = 0;
    };
    quic_crypto_result open_application(std::span<const std::byte> datagram, std::optional<std::size_t> cid_length,
                                      std::optional<std::uint64_t> largest, std::span<std::byte> output, std::span<std::byte> scratch) noexcept;
    quic_crypto_code traffic_install_allowed(quic_key_level level, quic_key_direction direction) const noexcept;
    quic_crypto_code write_allowed(quic_key_level level, std::uint64_t number) const noexcept;
    quic_crypto_code select_application_keys(const quic_clear_header& header, const quic_packet_keys*& selected, bool& promoting) const noexcept;
    static quic_crypto_code check_application_header(const quic_clear_header& header, void* argument) noexcept;
    void record_application_open(const quic_clear_header& header, bool promoting) noexcept;
    quic_crypto_code commit_write_update() noexcept;
    quic_crypto_result account_open(quic_crypto_result result) noexcept;
    void constrain_integrity(quic_cipher_suite suite) noexcept;
    std::array<level_state, 3> levels_;
    application_state application_;
    quic_usage_limits limits_;
    std::uint64_t failures_ = 0;
    quic_key_observer observer_;
    void* argument_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_KEY_STATE_HPP_
