/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include "./quic_crypto_provider.hpp"
namespace httpserver::detail {
namespace {
constexpr std::array<unsigned char, 16> retry_key{0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a, 0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e};
constexpr std::array<unsigned char, 12> retry_nonce{0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63, 0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb};
bool retry_aad(EVP_CIPHER_CTX* context, std::span<const std::byte> dcid, std::span<const std::byte> packet) noexcept {
    const auto size = static_cast<unsigned char>(dcid.size());
    int count = 0;
    if (EVP_CipherUpdate(context, nullptr, &count, &size, 1) != 1) return false;
    if (EVP_CipherUpdate(context, nullptr, &count, quic_data(dcid), static_cast<int>(dcid.size())) != 1) return false;
    return EVP_CipherUpdate(context, nullptr, &count, quic_data(packet), static_cast<int>(packet.size())) == 1;
}
quic_crypto_code finish_retry(EVP_CIPHER_CTX* context, std::span<std::byte> tag, bool encrypt) noexcept {
    if (!encrypt && EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_SET_TAG, 16, tag.data()) != 1) return quic_crypto_code::provider_failure;
    quic_temporary<16> empty;
    int count = 0;
    if (EVP_CipherFinal_ex(context, quic_data(empty.bytes), &count) != 1) return encrypt ? quic_crypto_code::provider_failure : quic_crypto_code::authentication_failed;
    if (encrypt && EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_GET_TAG, 16, tag.data()) != 1) return quic_crypto_code::provider_failure;
    return quic_crypto_code::ok;
}
quic_crypto_code retry_cipher(std::span<const std::byte> dcid, std::span<const std::byte> packet, std::span<std::byte> tag, bool encrypt) noexcept {
    if (dcid.size() > 20 || packet.size() > 65535) return quic_crypto_code::malformed;
    quic_cipher cipher(EVP_CIPHER_fetch(nullptr, "AES-128-GCM", nullptr), EVP_CIPHER_free);
    quic_cipher_context context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!cipher || !context) return quic_crypto_code::provider_failure;
    if (EVP_CipherInit_ex2(context.get(), cipher.get(), retry_key.data(), retry_nonce.data(), encrypt, nullptr) != 1) return quic_crypto_code::provider_failure;
    if (!retry_aad(context.get(), dcid, packet)) return quic_crypto_code::provider_failure;
    return finish_retry(context.get(), tag, encrypt);
}
}  // namespace
quic_crypto_code compute_quic_retry_tag(std::span<const std::byte> dcid, std::span<const std::byte> packet, std::span<std::byte> output) noexcept {
    if (output.size() < 16) return quic_crypto_code::no_space;
    if (quic_overlap(dcid, output) || quic_overlap(packet, output)) return quic_crypto_code::malformed;
    quic_temporary<16> tag;
    auto code = retry_cipher(dcid, packet, tag.bytes, true);
    if (code == quic_crypto_code::ok) std::copy(tag.bytes.begin(), tag.bytes.end(), output.begin());
    return code;
}
quic_crypto_code verify_quic_retry_tag(std::span<const std::byte> dcid, std::span<const std::byte> packet) noexcept {
    auto envelope = parse_quic_envelope(packet);
    if (envelope.code != quic_codec_code::ok || envelope.value.kind != quic_packet_kind::retry) return quic_crypto_code::malformed;
    quic_temporary<16> tag;
    std::copy(envelope.value.integrity_tag.begin(), envelope.value.integrity_tag.end(), tag.bytes.begin());
    return retry_cipher(dcid, packet.first(packet.size() - 16), tag.bytes, false);
}
}  // namespace httpserver::detail
