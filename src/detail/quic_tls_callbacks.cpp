/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include "detail/quic_tls_callbacks.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>
namespace httpserver::detail {
namespace {
std::optional<quic_cipher_suite> cipher_suite(unsigned id) {
    switch (id) {
        case 0x1301: return quic_cipher_suite::aes_128_gcm_sha256;
        case 0x1302: return quic_cipher_suite::aes_256_gcm_sha384;
        case 0x1303: return quic_cipher_suite::chacha20_poly1305_sha256;
        default: return std::nullopt;
    }
}
std::optional<quic_crypto_level> crypto_level(std::uint32_t level) {
    switch (level) {
        case OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE: return quic_crypto_level::handshake;
        case OSSL_RECORD_PROTECTION_LEVEL_APPLICATION: return quic_crypto_level::application;
        default: return std::nullopt;
    }
}
std::span<const std::byte> bytes(const unsigned char* data, std::size_t size) { return {reinterpret_cast<const std::byte*>(data), size}; }
template<class Function>
int boundary(void* argument, Function function) noexcept {
    auto& state = *static_cast<quic_tls_callbacks*>(argument);
    try {
        return function(state);
    } catch (...) {
        return state.fail(quic_tls_code::callback_error);
    }
}
int send_cb(SSL*, const unsigned char* data, std::size_t size, std::size_t* consumed, void* arg) noexcept {
    *consumed = 0;
    return boundary(arg, [&](auto& state) { return state.send(bytes(data, size), consumed); });
}
int recv_cb(SSL*, const unsigned char** data, std::size_t* size, void* arg) noexcept {
    *data = nullptr;
    *size = 0;
    return boundary(arg, [&](auto& state) { return state.recv(data, size); });
}
int release_cb(SSL*, std::size_t size, void* arg) noexcept {
    return boundary(arg, [&](auto& state) { return state.release(size); });
}
int secret_cb(SSL* ssl, std::uint32_t level, int direction, const unsigned char* data, std::size_t size, void* arg) noexcept {
    return boundary(arg, [&](auto& state) {
        const auto* cipher = SSL_get_current_cipher(ssl);
        return state.secret(level, direction, cipher ? SSL_CIPHER_get_protocol_id(cipher) : 0, bytes(data, size));
    });
}
int parameters_cb(SSL*, const unsigned char* data, std::size_t size, void* arg) noexcept {
    return boundary(arg, [&](auto& state) { return state.parameters(bytes(data, size)); });
}
int alert_cb(SSL*, unsigned char alert, void* arg) noexcept {
    return boundary(arg, [&](auto& state) { return state.alert(alert); });
}
}  // namespace
int quic_tls_callbacks::secret(std::uint32_t level, int direction, unsigned suite, std::span<const std::byte> data) noexcept {
    if (failure_.code != quic_tls_code::ok) return 0;
    if (direction != 0 && direction != 1) return fail(quic_tls_code::secret_error);
    if (level == OSSL_RECORD_PROTECTION_LEVEL_NONE) return data.empty() ? 1 : fail(quic_tls_code::secret_error);
    const auto mapped = crypto_level(level);
    const auto cipher = cipher_suite(suite);
    if (!mapped || !cipher) return fail(quic_tls_code::secret_error);
    return install_secret(*mapped, direction ? quic_key_direction::write : quic_key_direction::read, *cipher, data);
}
int quic_tls_callbacks::install_secret(quic_crypto_level level, quic_key_direction direction,
                                     quic_cipher_suite suite, std::span<const std::byte> data) noexcept {
    auto& current = direction == quic_key_direction::write ? write_level_ : read_level_;
    const auto previous = level == quic_crypto_level::handshake ? quic_crypto_level::initial : quic_crypto_level::handshake;
    if (current != previous) return fail(quic_tls_code::secret_error);
    const auto key_level = level == quic_crypto_level::handshake ? quic_key_level::handshake : quic_key_level::application;
    if (keys_.install_traffic_secret(key_level, direction, suite, data) != quic_crypto_code::ok) return fail(quic_tls_code::secret_error);
    current = level;
    return 1;
}
int quic_tls_callbacks::parameters(std::span<const std::byte> data) noexcept {
    if (failure_.code != quic_tls_code::ok) return 0;
    if (got_parameters_ || data.size() > peer_capacity_) return fail(quic_tls_code::transport_parameter_error);
    std::copy(data.begin(), data.end(), peer_.get());
    auto owned = std::span<const std::byte>(peer_.get(), data.size());
    const auto parsed = decode_quic_transport_parameters(owned, quic_endpoint_role::client);
    if (parsed.code != quic_codec_code::ok ||
        validate_quic_transport_parameters(parsed.value, quic_endpoint_role::client, peer_cids_) != quic_codec_code::ok) {
        return fail(quic_tls_code::transport_parameter_error);
    }
    peer_size_ = data.size();
    got_parameters_ = true;
    return 1;
}
const OSSL_DISPATCH* quic_tls_callbacks::dispatch() noexcept {
    static const OSSL_DISPATCH callbacks[] = {
        {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_SEND, reinterpret_cast<void (*)(void)>(send_cb)},
        {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RECV_RCD, reinterpret_cast<void (*)(void)>(recv_cb)},
        {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RELEASE_RCD, reinterpret_cast<void (*)(void)>(release_cb)},
        {OSSL_FUNC_SSL_QUIC_TLS_YIELD_SECRET, reinterpret_cast<void (*)(void)>(secret_cb)},
        {OSSL_FUNC_SSL_QUIC_TLS_GOT_TRANSPORT_PARAMS, reinterpret_cast<void (*)(void)>(parameters_cb)},
        {OSSL_FUNC_SSL_QUIC_TLS_ALERT, reinterpret_cast<void (*)(void)>(alert_cb)},
        {0, nullptr}
    };
    return callbacks;
}
void quic_tls_callbacks::install(SSL* ssl) {
    auto params = local_parameters();
    if (SSL_set_quic_tls_cbs(ssl, dispatch(), this) != 1 ||
        SSL_set_quic_tls_transport_params(ssl, reinterpret_cast<const unsigned char*>(params.data()), params.size()) != 1 ||
        SSL_set_quic_tls_early_data_enabled(ssl, 0) != 1) throw std::runtime_error("QUIC TLS callbacks unavailable");
}
}  // namespace httpserver::detail
