/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_tls_session.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_TLS_SESSION_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_TLS_SESSION_HPP_
#include <memory>
#include <optional>
#include <httpserver/detail/quic_key_state.hpp>
#include <httpserver/detail/quic_reassembly.hpp>
#include <httpserver/detail/quic_transport_parameters.hpp>
#include <httpserver/detail/tls_credentials.hpp>
#include <httpserver/detail/tls_session.hpp>
namespace httpserver::detail {
enum class quic_crypto_level { initial, handshake, application };
enum class quic_tls_code {
    ok, invalid_level, invalid_offset, input_limit, no_memory, callback_error,
    secret_error, transport_parameter_error, provider_error, alert
};
struct quic_tls_result {
    quic_tls_code code = quic_tls_code::ok;
    std::size_t bytes = 0;
    quic_stream_code stream_code = quic_stream_code::ok;
    explicit operator bool() const noexcept { return code == quic_tls_code::ok; }
};
struct quic_tls_failure {
    quic_tls_code code = quic_tls_code::ok;
    std::optional<unsigned char> alert;
};
struct quic_tls_config {
    quic_stream_limits input_limits;
    std::size_t output_capacity = 65536;  // Each level; retained until explicit retirement.
    std::size_t receive_lease_capacity = 16384;
    std::size_t maximum_peer_parameters = 4096;
    std::span<const std::byte> local_parameters;
    quic_parameter_cid_context peer_cids;
};
// Owner-thread serialized. Configuration spans are copied at construction.
// keys must outlive this session. copy_output never consumes retransmittable data;
// retirement policy and packet scheduling belong to the connection owner.
class quic_tls_session final {
 public:
    quic_tls_session(tls_credentials_selection selection, const quic_tls_config& config,
                     server::resource_budget budget, quic_key_state& keys);
    quic_tls_session(tls_credentials_selection selection, const quic_tls_config& config, quic_storage_lease storage, quic_key_state& keys);
    ~quic_tls_session();
    quic_tls_result receive(quic_crypto_level level, std::uint64_t offset, std::span<const std::byte> bytes);
    tls_session::result handshake();
    tls_session::result process_post_handshake();
    quic_tls_result copy_output(quic_crypto_level level, std::uint64_t offset, std::span<std::byte> destination) const;
    quic_tls_result retire_output_prefix(quic_crypto_level level, std::uint64_t through_offset);
    std::span<const std::byte> peer_transport_parameter_bytes() const;
    tls_negotiated_protocol negotiated_protocol() const;
    std::shared_ptr<const server::tls_peer_metadata> peer_metadata() const;
    quic_tls_failure failure() const;

 private:
    quic_storage_lease storage_owner_;
    struct impl;
    std::unique_ptr<impl> impl_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_TLS_SESSION_HPP_
