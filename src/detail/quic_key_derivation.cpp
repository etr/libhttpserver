/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/core_names.h>
#include <openssl/kdf.h>
#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>
#include "./quic_crypto_provider.hpp"
namespace httpserver::detail {
namespace {
bool hkdf(const char* digest, int mode, std::span<const std::byte> key, std::span<const std::byte> salt,
          std::span<const std::byte> info, std::span<std::byte> output) noexcept {
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> algorithm(EVP_KDF_fetch(nullptr, "HKDF", nullptr), EVP_KDF_free);
    if (!algorithm) return false;
    std::unique_ptr<EVP_KDF_CTX, decltype(&EVP_KDF_CTX_free)> context(EVP_KDF_CTX_new(algorithm.get()), EVP_KDF_CTX_free);
    if (!context) return false;
    std::array<OSSL_PARAM, 6> params{
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char*>(digest), 0),
        OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<std::byte*>(key.data()), key.size()),
        OSSL_PARAM_construct_end()};
    std::size_t index = 3;
    if (!salt.empty()) params[index++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<std::byte*>(salt.data()), salt.size());
    if (!info.empty()) params[index++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<std::byte*>(info.data()), info.size());
    params[index] = OSSL_PARAM_construct_end();
    return EVP_KDF_derive(context.get(), quic_data(output), output.size(), params.data()) == 1;
}
bool expand(const char* digest, std::span<const std::byte> secret, std::string_view label, std::span<std::byte> output) noexcept {
    quic_temporary<64> info;
    constexpr std::string_view prefix = "tls13 ";
    const auto length = prefix.size() + label.size();
    info.bytes[0] = std::byte(output.size() >> 8);
    info.bytes[1] = std::byte(output.size());
    info.bytes[2] = std::byte(length);
    std::transform(prefix.begin(), prefix.end(), info.bytes.begin() + 3, [](char c) { return std::byte(c); });
    std::transform(label.begin(), label.end(), info.bytes.begin() + 3 + prefix.size(), [](char c) { return std::byte(c); });
    return hkdf(digest, EVP_KDF_HKDF_MODE_EXPAND_ONLY, secret, {}, std::span(info.bytes).first(length + 4), output);
}
quic_crypto_code derive_material(quic_cipher_suite suite, std::span<const std::byte> secret, std::span<const std::byte> retained_hp,
                                quic_packet_keys& output, quic_key_observer observer, void* argument) {
    const auto spec = quic_suite(suite);
    if (!spec) return quic_crypto_code::unsupported_suite;
    if (secret.size() != spec->secret_size) return quic_crypto_code::malformed;
    quic_temporary<32> key, hp;
    quic_temporary<12> iv;
    auto key_view = std::span(key.bytes).first(spec->key_size), hp_view = std::span(hp.bytes).first(spec->key_size);
    if (!expand(spec->digest, secret, "quic key", key_view)) return quic_crypto_code::provider_failure;
    if (!expand(spec->digest, secret, "quic iv", iv.bytes)) return quic_crypto_code::provider_failure;
    if (retained_hp.empty() && !expand(spec->digest, secret, "quic hp", hp_view)) return quic_crypto_code::provider_failure;
    quic_packet_keys staged;
    staged.suite = suite;
    staged.secret = secure_bytes(secret, observer, argument);
    staged.key = secure_bytes(key_view, observer, argument);
    staged.iv = secure_bytes(iv.bytes, observer, argument);
    staged.hp = secure_bytes(retained_hp.empty() ? hp_view : retained_hp, observer, argument);
    output = std::move(staged);
    return quic_crypto_code::ok;
}
quic_crypto_code initial_material(std::span<const std::byte> dcid, quic_initial_keys& output, quic_key_observer observer, void* argument) {
    if (dcid.size() > 20) return quic_crypto_code::malformed;
    constexpr std::array<unsigned char, 20> salt{0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17, 0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a};
    quic_temporary<32> initial, client, server;
    if (!hkdf("SHA256", EVP_KDF_HKDF_MODE_EXTRACT_ONLY, dcid, std::as_bytes(std::span(salt)), {}, initial.bytes)) return quic_crypto_code::provider_failure;
    if (!expand("SHA256", initial.bytes, "client in", client.bytes)) return quic_crypto_code::provider_failure;
    if (!expand("SHA256", initial.bytes, "server in", server.bytes)) return quic_crypto_code::provider_failure;
    quic_initial_keys staged;
    auto code = derive_material(quic_cipher_suite::aes_128_gcm_sha256, client.bytes, {}, staged.client, observer, argument);
    if (code != quic_crypto_code::ok) return code;
    code = derive_material(quic_cipher_suite::aes_128_gcm_sha256, server.bytes, {}, staged.server, observer, argument);
    if (code != quic_crypto_code::ok) return code;
    staged.client.level = staged.server.level = quic_key_level::initial;
    output = std::move(staged);
    return quic_crypto_code::ok;
}
}  // namespace
quic_crypto_code derive_quic_initial_keys(std::span<const std::byte> dcid, quic_initial_keys& output, quic_key_observer observer, void* argument) noexcept {
    try {
        return initial_material(dcid, output, observer, argument);
    } catch (...) {
        return quic_crypto_code::provider_failure;
    }
}
quic_crypto_code derive_quic_packet_keys(quic_cipher_suite suite, std::span<const std::byte> secret, quic_packet_keys& output, quic_key_observer observer, void* argument) noexcept {
    try {
        return derive_material(suite, secret, {}, output, observer, argument);
    } catch (...) {
        return quic_crypto_code::provider_failure;
    }
}
quic_crypto_code derive_quic_next_keys(const quic_packet_keys& current, quic_packet_keys& output, quic_key_observer observer, void* argument) noexcept {
    const auto spec = quic_suite(current.suite);
    if (!spec) return quic_crypto_code::unsupported_suite;
    if (current.level != quic_key_level::application || current.secret.bytes().size() != spec->secret_size || !quic_keys_valid(current)) return quic_crypto_code::invalid_key_transition;
    quic_temporary<48> next;
    auto bytes = std::span(next.bytes).first(spec->secret_size);
    if (!expand(spec->digest, current.secret.bytes(), "quic ku", bytes)) return quic_crypto_code::provider_failure;
    try {
        return derive_material(current.suite, bytes, current.hp.bytes(), output, observer, argument);
    } catch (...) {
        return quic_crypto_code::provider_failure;
    }
}
}  // namespace httpserver::detail
