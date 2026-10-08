/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_DETAIL_QUIC_TLS_CALLBACKS_HPP_
#define SRC_DETAIL_QUIC_TLS_CALLBACKS_HPP_
#include <openssl/ssl.h>
#include <openssl/core_dispatch.h>
#include <array>
#include <memory>
#include <httpserver/detail/quic_tls_session.hpp>
namespace httpserver::detail {
// Private provider seam. Fixed-address receive lease survives level changes and
// additional inserts. This state must outlive SSL_free (which can release it).
class quic_tls_callbacks final {
 public:
    quic_tls_callbacks(const quic_tls_config& config, server::resource_budget budget, quic_key_state& keys);
    quic_tls_result receive(quic_crypto_level level, std::uint64_t offset, std::span<const std::byte> bytes);
    quic_tls_result copy_output(quic_crypto_level level, std::uint64_t offset, std::span<std::byte> destination) const;
    quic_tls_result retire_output_prefix(quic_crypto_level level, std::uint64_t through);
    int send(std::span<const std::byte> bytes, std::size_t* consumed) noexcept;
    int recv(const unsigned char** buffer, std::size_t* size) noexcept;
    int release(std::size_t size) noexcept;
    int secret(std::uint32_t level, int direction, unsigned suite, std::span<const std::byte> bytes) noexcept;
    int parameters(std::span<const std::byte> bytes) noexcept;
    int fail(quic_tls_code code) noexcept;
    int alert(unsigned char code) noexcept;
    quic_tls_failure failure() const noexcept { return failure_; }
    std::span<const std::byte> local_parameters() const { return {local_.get(), local_size_}; }
    std::span<const std::byte> peer_parameters() const { return {peer_.get(), peer_size_}; }
    static const OSSL_DISPATCH* dispatch() noexcept;
    void install(SSL* ssl);

 private:
    int install_secret(quic_crypto_level level, quic_key_direction direction, quic_cipher_suite suite, std::span<const std::byte> data) noexcept;
    struct output_stream {
        std::unique_ptr<std::byte[]> bytes;
        std::uint64_t begin = 0;
        std::size_t size = 0;
    };
    server::reservation storage_;
    std::array<output_stream, 3> output_;
    std::size_t output_capacity_, lease_capacity_, peer_capacity_, local_size_, peer_size_ = 0;
    std::unique_ptr<std::byte[]> lease_, local_, peer_;
    std::size_t leased_size_ = 0;
    quic_crypto_level read_level_ = quic_crypto_level::initial, write_level_ = quic_crypto_level::initial;
    quic_crypto_level leased_level_ = quic_crypto_level::initial;
    std::array<std::byte, 20> initial_cid_{}, original_cid_{}, retry_cid_{};
    quic_parameter_cid_context peer_cids_;
    quic_key_state& keys_;
    std::array<quic_reassembly, 3> input_;
    quic_tls_failure failure_;
    bool got_parameters_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_DETAIL_QUIC_TLS_CALLBACKS_HPP_
