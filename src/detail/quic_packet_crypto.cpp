/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include "./quic_crypto_provider.hpp"
namespace httpserver::detail {
namespace {
constexpr std::size_t maximum_packet = 65535;
bool buffers_overlap(std::span<const std::byte> input, std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    return quic_overlap(input, output) || quic_overlap(input, scratch) || quic_overlap(output, scratch);
}
bool write_overlaps(const quic_packet_write& p, std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    for (auto view : {p.destination, p.source, p.token, p.payload, p.tag, p.versions}) {
        if (buffers_overlap(view, output, scratch)) return true;
    }
    return false;
}
bool key_overlaps(const quic_packet_keys& keys, std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    for (auto view : {keys.key.bytes(), keys.iv.bytes(), keys.hp.bytes(), keys.secret.bytes()}) {
        if (quic_overlap(view, output) || quic_overlap(view, scratch)) return true;
    }
    return false;
}
quic_crypto_code codec_error(quic_codec_code code) noexcept {
    if (code == quic_codec_code::no_space) return quic_crypto_code::no_space;
    if (code == quic_codec_code::truncated) return quic_crypto_code::truncated;
    if (code == quic_codec_code::limit_exceeded) return quic_crypto_code::limit_reached;
    return quic_crypto_code::malformed;
}
bool add_aad(EVP_CIPHER_CTX* context, std::span<const std::byte> header) noexcept {
    int count = 0;
    return EVP_CipherUpdate(context, nullptr, &count, quic_data(header), static_cast<int>(header.size())) == 1;
}
bool initialize_payload(EVP_CIPHER_CTX* context, const EVP_CIPHER* cipher, const quic_packet_keys& keys,
                        std::span<const std::byte> nonce, std::span<std::byte> tag, bool encrypt) noexcept {
    if (EVP_CipherInit_ex2(context, cipher, nullptr, nullptr, encrypt, nullptr) != 1) return false;
    // ChaCha20-Poly1305 may need the expected tag before its IV is set.
    if (!encrypt && EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_SET_TAG, 16, tag.data()) != 1) return false;
    return EVP_CipherInit_ex2(context, nullptr, quic_data(keys.key.bytes()), quic_data(nonce), encrypt, nullptr) == 1;
}
quic_crypto_code finish_payload(EVP_CIPHER_CTX* context, std::span<std::byte> payload_and_tag, int count, bool encrypt) noexcept {
    int final_count = 0;
    if (EVP_CipherFinal_ex(context, quic_data(payload_and_tag) + count, &final_count) != 1)
        return encrypt ? quic_crypto_code::provider_failure : quic_crypto_code::authentication_failed;
    if (static_cast<std::size_t>(count + final_count) != payload_and_tag.size() - 16) return quic_crypto_code::provider_failure;
    if (encrypt && EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_AEAD_GET_TAG, 16, payload_and_tag.last(16).data()) != 1) return quic_crypto_code::provider_failure;
    return quic_crypto_code::ok;
}
quic_crypto_code payload_cipher(const quic_packet_keys& keys, std::uint64_t number, std::span<const std::byte> header,
                               std::span<std::byte> payload_and_tag, bool encrypt) noexcept {
    quic_temporary<12> nonce;
    auto code = quic_packet_nonce(keys, number, nonce.bytes);
    if (code != quic_crypto_code::ok) return code;
    quic_cipher cipher(EVP_CIPHER_fetch(nullptr, quic_suite(keys.suite)->aead, nullptr), EVP_CIPHER_free);
    quic_cipher_context context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!cipher || !context) return quic_crypto_code::provider_failure;
    if (!initialize_payload(context.get(), cipher.get(), keys, nonce.bytes, payload_and_tag.last(16), encrypt)) return quic_crypto_code::provider_failure;
    if (!add_aad(context.get(), header)) return quic_crypto_code::provider_failure;
    auto payload = payload_and_tag.first(payload_and_tag.size() - 16);
    int count = 0;
    if (EVP_CipherUpdate(context.get(), quic_data(payload), &count, quic_data(payload), static_cast<int>(payload.size())) != 1) return quic_crypto_code::provider_failure;
    return finish_payload(context.get(), payload_and_tag, count, encrypt);
}
quic_crypto_code apply_mask(const quic_packet_keys& keys, std::span<std::byte> packet, std::size_t offset, std::size_t& width, bool removing) noexcept {
    if (packet.size() - offset < 20) return quic_crypto_code::truncated;
    quic_temporary<5> mask;
    auto code = quic_header_mask(keys, packet.subspan(offset + 4, 16), mask.bytes);
    if (code != quic_crypto_code::ok) return code;
    auto first_mask = (std::to_integer<unsigned>(packet[0]) & 0x80) ? 0x0f : 0x1f;
    packet[0] ^= mask.bytes[0] & std::byte(first_mask);
    if (removing) width = 1 + (std::to_integer<unsigned>(packet[0]) & 3);
    for (std::size_t n = 0; n < width; ++n) packet[offset + n] ^= mask.bytes[n + 1];
    return quic_crypto_code::ok;
}
quic_crypto_code validate_keys(const quic_packet_keys& keys, quic_packet_kind kind) noexcept {
    if (!quic_suite(keys.suite)) return quic_crypto_code::unsupported_suite;
    if (!quic_keys_valid(keys)) return quic_crypto_code::keys_unavailable;
    if (kind == quic_packet_kind::initial && keys.suite != quic_cipher_suite::aes_128_gcm_sha256) return quic_crypto_code::invalid_key_transition;
    return quic_level_matches(keys, kind) ? quic_crypto_code::ok : quic_crypto_code::invalid_key_transition;
}
quic_crypto_code mask_preflight(const quic_packet_keys& keys, std::span<const std::byte> sample, std::span<std::byte> output) noexcept {
    const auto spec = quic_suite(keys.suite);
    if (!spec) return quic_crypto_code::unsupported_suite;
    if (keys.hp.bytes().size() != spec->key_size) return quic_crypto_code::keys_unavailable;
    if (sample.size() != 16) return quic_crypto_code::truncated;
    if (output.size() < 5) return quic_crypto_code::no_space;
    if (quic_overlap(sample, output) || quic_overlap(keys.hp.bytes(), output)) return quic_crypto_code::malformed;
    return quic_crypto_code::ok;
}
bool generate_mask(const quic_packet_keys& keys, std::span<const std::byte> sample, std::span<std::byte> output) noexcept {
    quic_cipher cipher(EVP_CIPHER_fetch(nullptr, quic_suite(keys.suite)->protection, nullptr), EVP_CIPHER_free);
    quic_cipher_context context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!cipher || !context) return false;
    const bool chacha = keys.suite == quic_cipher_suite::chacha20_poly1305_sha256;
    if (EVP_EncryptInit_ex2(context.get(), cipher.get(), quic_data(keys.hp.bytes()), chacha ? quic_data(sample) : nullptr, nullptr) != 1) return false;
    if (EVP_CIPHER_CTX_set_padding(context.get(), 0) != 1) return false;
    constexpr std::array<std::byte, 5> zero{};
    const auto input = chacha ? std::span<const std::byte>(zero) : sample;
    int count = 0, tail = 0;
    if (EVP_EncryptUpdate(context.get(), quic_data(output), &count, quic_data(input), static_cast<int>(input.size())) != 1) return false;
    return EVP_EncryptFinal_ex(context.get(), quic_data(output) + count, &tail) == 1;
}
quic_crypto_code write_preflight(const quic_packet_keys& keys, const quic_packet_write& packet,
                                 std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    if (write_overlaps(packet, output, scratch) || key_overlaps(keys, output, scratch)) return quic_crypto_code::malformed;
    if (!packet.tag.empty()) return quic_crypto_code::malformed;
    if (packet.payload.size() > maximum_packet || packet.token.size() > maximum_packet) return quic_crypto_code::limit_reached;
    return validate_keys(keys, packet.kind);
}
quic_crypto_result protect_staged(const quic_packet_keys& keys, const quic_packet_write& packet,
                                  std::span<std::byte> staged, std::span<std::byte> output) noexcept {
    const auto envelope = parse_quic_envelope(staged, packet.destination.size());
    if (envelope.code != quic_codec_code::ok) return {codec_error(envelope.code)};
    const auto offset = envelope.value.packet_number_offset;
    if (staged.size() - offset < 20) return {quic_crypto_code::truncated};
    const auto header_size = offset + packet.packet_number_width;
    auto code = payload_cipher(keys, packet.packet_number, staged.first(header_size), staged.subspan(header_size), true);
    if (code != quic_crypto_code::ok) return {code};
    auto width = packet.packet_number_width;
    code = apply_mask(keys, staged, offset, width, false);
    if (code != quic_crypto_code::ok) return {code};
    std::copy(staged.begin(), staged.end(), output.begin());
    return {quic_crypto_code::ok, staged.size(), packet.payload.size()};
}
quic_crypto_result authenticate_staged(const quic_packet_keys& keys, const quic_packet_envelope& envelope,
                                      std::optional<std::uint64_t> largest, std::span<std::byte> staged, std::span<std::byte> output, quic_authenticated_check check) noexcept {
    const auto offset = envelope.packet_number_offset;
    std::size_t width = 0;
    auto code = apply_mask(keys, staged, offset, width, true);
    if (code != quic_crypto_code::ok) return {code};
    const auto header = decode_quic_unprotected_header(envelope, {std::to_integer<std::uint8_t>(staged[0]), staged.subspan(offset, width)}, largest);
    if (header.code != quic_codec_code::ok) return {codec_error(header.code)};
    auto payload = staged.subspan(offset + width);
    if (payload.size() < 16) return {quic_crypto_code::truncated};
    if (output.size() < payload.size() - 16) return {quic_crypto_code::no_space};
    code = payload_cipher(keys, header.value.packet_number, staged.first(offset + width), payload, false);
    if (code != quic_crypto_code::ok) return {code};
    if (validate_quic_authenticated_header(header.value) != quic_codec_code::ok) return {};
    if (check.function) {
        code = check.function(header.value, check.argument);
        if (code != quic_crypto_code::ok) return {code};
    }
    std::copy_n(payload.begin(), payload.size() - 16, output.begin());
    return {quic_crypto_code::ok, staged.size(), payload.size() - 16, header.value};
}
}  // namespace
quic_crypto_code quic_packet_nonce(const quic_packet_keys& keys, std::uint64_t number, std::span<std::byte> output) noexcept {
    if (keys.iv.bytes().size() != 12) return quic_crypto_code::keys_unavailable;
    if (number > k_quic_max_integer) return quic_crypto_code::malformed;
    if (output.size() < 12) return quic_crypto_code::no_space;
    if (quic_overlap(keys.iv.bytes(), output)) return quic_crypto_code::malformed;
    std::copy(keys.iv.bytes().begin(), keys.iv.bytes().end(), output.begin());
    for (std::size_t i = 0; i < 8; ++i) output[11 - i] ^= std::byte(number >> (8 * i));
    return quic_crypto_code::ok;
}
quic_crypto_code quic_header_mask(const quic_packet_keys& keys, std::span<const std::byte> sample, std::span<std::byte> output) noexcept {
    auto code = mask_preflight(keys, sample, output);
    if (code != quic_crypto_code::ok) return code;
    quic_temporary<32> mask;
    if (!generate_mask(keys, sample, mask.bytes)) return quic_crypto_code::provider_failure;
    std::copy_n(mask.bytes.begin(), 5, output.begin());
    return quic_crypto_code::ok;
}
quic_crypto_result protect_quic_packet(const quic_packet_keys& keys, const quic_packet_write& packet,
                                      std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    auto code = write_preflight(keys, packet, output, scratch);
    if (code != quic_crypto_code::ok) return {code};
    scratch = scratch.first(std::min(scratch.size(), maximum_packet));
    quic_scratch_guard guard{scratch};
    std::array<std::byte, 16> tag{};
    auto clear = packet;
    clear.tag = tag;
    auto encoded = encode_quic_packet(clear, scratch);
    if (encoded.code != quic_codec_code::ok) return {codec_error(encoded.code)};
    auto staged = scratch.first(encoded.consumed);
    if (output.size() < staged.size()) return {quic_crypto_code::no_space};
    return protect_staged(keys, packet, staged, output);
}
quic_crypto_result quic_open_checked(const quic_packet_keys& keys, std::span<const std::byte> datagram,
                                        std::optional<std::size_t> cid_length, std::optional<std::uint64_t> largest,
                                        std::span<std::byte> output, std::span<std::byte> scratch, quic_authenticated_check check) noexcept {
    if (buffers_overlap(datagram, output, scratch) || key_overlaps(keys, output, scratch)) return {};
    const auto envelope = parse_quic_envelope(datagram, cid_length);
    if (envelope.code != quic_codec_code::ok) return {codec_error(envelope.code)};
    auto code = validate_keys(keys, envelope.value.kind);
    if (code != quic_crypto_code::ok) return {code};
    const auto packet = envelope.value.packet;
    if (scratch.size() < packet.size()) return {quic_crypto_code::no_space};
    auto staged = scratch.first(packet.size());
    quic_scratch_guard guard{staged};
    std::copy(packet.begin(), packet.end(), staged.begin());
    return authenticate_staged(keys, envelope.value, largest, staged, output, check);
}
quic_crypto_result unprotect_quic_packet(const quic_packet_keys& keys, std::span<const std::byte> datagram,
                                        std::optional<std::size_t> cid_length, std::optional<std::uint64_t> largest,
                                        std::span<std::byte> output, std::span<std::byte> scratch) noexcept {
    return quic_open_checked(keys, datagram, cid_length, largest, output, scratch, {});
}
}  // namespace httpserver::detail
