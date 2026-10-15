/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/digest_nonce.hpp>
#include <string>

namespace httpserver::detail::digest::nonce_detail {

void store_be64(std::byte* out, std::uint64_t value) noexcept {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::byte>((value >> (56 - 8 * i)) & 0xff);
    }
}

std::uint64_t load_be64(const std::byte* in) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(static_cast<unsigned char>(in[i]));
    }
    return value;
}

std::string nonce_head_hex(std::uint64_t now_unix, std::span<const std::byte> entropy) {
    std::byte stamp[8];
    store_be64(stamp, now_unix);
    std::string head = encode_hex(stamp);
    head += encode_hex(entropy);
    return head;
}

std::string nonce_tag_hex(std::string_view head_hex, const nonce_key& key) {
    std::string mac(k_mac_prefix);
    mac += head_hex;
    mac.push_back('|');
    mac += encode_hex(key);
    const std::byte* raw = reinterpret_cast<const std::byte*>(mac.data());
    const std::array<std::byte, 32> tag = sha256(std::span<const std::byte>(raw, mac.size()));
    return encode_hex(tag);
}

}  // namespace httpserver::detail::digest::nonce_detail

namespace httpserver::detail::digest {

std::optional<std::string> mint_nonce(const nonce_key& key, std::uint64_t now_unix, entropy_fill fill) {
    std::byte entropy[16];
    const std::span<std::byte> draw(entropy);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (fill(draw).ok()) {
            using nonce_detail::nonce_head_hex;
            using nonce_detail::nonce_tag_hex;
            const std::string head = nonce_head_hex(now_unix, draw);
            return head + nonce_tag_hex(head, key);
        }
    }
    return std::nullopt;
}

nonce_class classify_nonce(std::string_view nonce, const nonce_key& key, std::uint64_t now_unix, std::chrono::seconds ttl) {
    if (nonce.size() != k_nonce_hex_length) return nonce_class::invalid;
    const std::optional<std::vector<std::byte>> decoded = decode_hex(nonce);
    if (!decoded.has_value()) return nonce_class::invalid;

    using nonce_detail::load_be64;
    using nonce_detail::nonce_tag_hex;
    const std::string head(nonce.substr(0, k_nonce_hex_length - 64));
    if (!constant_time_equal(nonce.substr(k_nonce_hex_length - 64), nonce_tag_hex(head, key))) {
        return nonce_class::invalid;
    }
    const std::uint64_t stamped = load_be64(decoded->data());
    const std::int64_t ttl_seconds = static_cast<std::int64_t>(ttl.count());
    if (static_cast<std::int64_t>(stamped) + ttl_seconds < static_cast<std::int64_t>(now_unix)) {
        return nonce_class::expired;
    }
    if (stamped > now_unix + static_cast<std::uint64_t>(k_future_skew.count())) {
        return nonce_class::future;
    }
    return nonce_class::fresh;
}

std::optional<nonce_key> mint_key() {
    nonce_key key{};
    const std::span<std::byte> draw(key.data(), key.size());
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (entropy::fill(draw).ok()) return key;
    }
    return std::nullopt;
}

}  // namespace httpserver::detail::digest
