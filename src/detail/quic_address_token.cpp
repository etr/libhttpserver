/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <vector>
#include <httpserver/detail/quic_address_token.hpp>
#include "./quic_crypto_provider.hpp"
namespace httpserver::detail {
namespace {
void append_integer(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t width) {
    for (std::size_t i = width; i > 0; --i) bytes.push_back(std::byte((value >> ((i - 1) * 8)) & 255));
}
void append_endpoint(std::vector<std::byte>& bytes, const datagram_endpoint& endpoint) {
    append_integer(bytes, static_cast<unsigned>(endpoint.peer.address.family), 1);
    bytes.insert(bytes.end(), endpoint.peer.address.bytes.begin(), endpoint.peer.address.bytes.end());
    append_integer(bytes, endpoint.peer.port, 2);
    append_integer(bytes, endpoint.scope, 4);
}
std::vector<std::byte> token_context(const quic_path_identity& path, std::uint64_t listener) {
    std::vector<std::byte> bytes;
    bytes.reserve(70);
    append_endpoint(bytes, path.peer);
    append_integer(bytes, path.local.has_value(), 1);
    if (path.local) append_endpoint(bytes, *path.local);
    append_integer(bytes, path.interface_index.has_value(), 1);
    if (path.interface_index) append_integer(bytes, *path.interface_index, 4);
    append_integer(bytes, path.socket_id, 8);
    append_integer(bytes, listener, 8);
    return bytes;
}
void append_cid(std::vector<std::byte>& bytes, const quic_cid& cid) {
    append_integer(bytes, cid.size, 1);
    bytes.insert(bytes.end(), cid.bytes.begin(), cid.bytes.begin() + cid.size);
}
bool read_cid(std::span<const std::byte> bytes, std::size_t& cursor, quic_cid& cid) {
    if (cursor >= bytes.size()) return false;
    cid.size = std::to_integer<std::size_t>(bytes[cursor++]);
    if (cid.size == 0 || cid.size > k_quic_cid_bytes || cid.size > bytes.size() - cursor) return false;
    std::copy_n(bytes.begin() + cursor, cid.size, cid.bytes.begin());
    cursor += cid.size;
    return true;
}
bool valid_token_prefix(std::span<const std::byte> token, std::uint64_t now) {
    if (token.size() < 46 || token.size() > quic_address_token::maximum_size) return false;
    if (token[0] != std::byte{1} || token[1] != std::byte{0x52}) return false;
    std::uint64_t issued = 0;
    for (std::size_t i = 2; i < 10; ++i) issued = (issued << 8) | std::to_integer<unsigned>(token[i]);
    return issued <= now && now - issued <= quic_address_token::lifetime_seconds;
}
}  // namespace
quic_address_token::quic_address_token(std::span<const std::byte> key) {
    if (!key.empty()) {
        if (key.size() != 32) throw std::invalid_argument("httpserver: QUIC token secret requires 32 bytes");
        secret_ = secure_bytes(key);
        return;
    }
    quic_temporary<32> random;
    if (RAND_priv_bytes(quic_data(random.bytes), random.bytes.size()) != 1) {
        throw std::runtime_error("httpserver: QUIC token randomness failed");
    }
    secret_ = secure_bytes(random.bytes);
}
bool quic_address_token::mac(std::span<const std::byte> payload, const quic_path_identity& path,
                             std::uint64_t listener, std::span<std::byte> output) const {
    std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> algorithm(EVP_MAC_fetch(nullptr, "HMAC", nullptr), EVP_MAC_free);
    if (!algorithm) return false;
    std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> context(EVP_MAC_CTX_new(algorithm.get()), EVP_MAC_CTX_free);
    if (!context) return false;
    char digest[] = "SHA256";
    OSSL_PARAM params[]{OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest, 0), OSSL_PARAM_construct_end()};
    if (EVP_MAC_init(context.get(), quic_data(secret_.bytes()), secret_.bytes().size(), params) != 1) return false;
    auto binding = token_context(path, listener);
    if (EVP_MAC_update(context.get(), quic_data(payload), payload.size()) != 1) return false;
    if (EVP_MAC_update(context.get(), quic_data(std::span<const std::byte>(binding)), binding.size()) != 1) return false;
    std::size_t size = 0;
    return EVP_MAC_final(context.get(), quic_data(output), &size, output.size()) == 1 && size == 32;
}
std::optional<std::vector<std::byte>> quic_address_token::issue(const quic_token_claims& claims,
    const quic_path_identity& path, std::uint64_t listener, std::uint64_t now) const {
    for (const auto* cid : {&claims.original_destination, &claims.retry_source}) {
        if (cid->size == 0 || cid->size > k_quic_cid_bytes) return std::nullopt;
    }
    std::vector<std::byte> token{std::byte{1}, std::byte{0x52}};
    token.reserve(maximum_size);
    append_integer(token, now, 8);
    append_cid(token, claims.original_destination);
    append_cid(token, claims.retry_source);
    quic_temporary<32> tag;
    if (!mac(token, path, listener, tag.bytes)) return std::nullopt;
    token.insert(token.end(), tag.bytes.begin(), tag.bytes.end());
    return token;
}
std::optional<quic_token_claims> quic_address_token::verify(std::span<const std::byte> token,
    const quic_path_identity& path, std::uint64_t listener, const quic_cid& retry, std::uint64_t now) const {
    if (!valid_token_prefix(token, now)) return std::nullopt;
    const auto payload = token.first(token.size() - 32);
    std::size_t cursor = 10;
    quic_token_claims claims;
    if (!read_cid(payload, cursor, claims.original_destination)) return std::nullopt;
    if (!read_cid(payload, cursor, claims.retry_source)) return std::nullopt;
    if (cursor != payload.size() || !(claims.retry_source == retry)) return std::nullopt;
    quic_temporary<32> tag;
    if (!mac(payload, path, listener, tag.bytes)) return std::nullopt;
    if (CRYPTO_memcmp(tag.bytes.data(), token.data() + payload.size(), 32) != 0) return std::nullopt;
    return claims;
}
}  // namespace httpserver::detail
