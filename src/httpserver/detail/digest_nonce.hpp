/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, see the file LICENSE in
     the distribution; if not, write to the Free Software Foundation,
     Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/

// The v3 Digest nonce (TASK-115, plan section 3): mint and classify,
// no library state. A nonce is 112 lowercase hex characters over 56
// bytes:
//
//   t   (8 bytes)  big-endian wall-clock seconds at mint time
//   r   (16 bytes) OS entropy
//   tag (32 bytes) SHA-256 over
//                  "libhttpserver-v3-nonce-v1|" hex(t) "|" hex(r) "|"
//                  hex(key)
//
// The tag is a suffix-key MAC (SHA-256, always -- never the
// negotiated Digest algorithm): no in-tree HMAC exists, and the
// suffix construction resists SHA-256's length-extension. Forging
// impact is limited to forcing 401 challenges; the HA1 secret is
// still required to authenticate. The tag lets the server tell its
// OWN expired nonces (stale_nonce, re-challenge with stale=TRUE)
// from forged or foreign ones (credentials_rejected, no stale) even
// after the ledger has evicted the slot.
//
// Classification is pure and clock-injected for testability; the
// constant-time tag comparison keeps the key's Mac output free of
// early-exit timing. k_future_skew bounds clock disagreement: a
// nonce stamped more than 60 seconds ahead of the observing clock is
// refused without stale (a minted-then-sent nonce cannot be that
// far ahead on healthy clocks).

#if !defined(HTTPSERVER_COMPILATION)
#error "digest_nonce.hpp is internal; only reachable when compiling libhttpserver."
#endif

#ifndef SRC_HTTPSERVER_DETAIL_DIGEST_NONCE_HPP_
#define SRC_HTTPSERVER_DETAIL_DIGEST_NONCE_HPP_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <httpserver/detail/digest_hex.hpp>
#include <httpserver/detail/entropy_sys.hpp>
#include <httpserver/detail/secure_compare.hpp>
#include <httpserver/detail/sha256.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver {

namespace detail {

namespace digest {

// The per-policy MAC key: 32 bytes of OS entropy fixed at creation.
using nonce_key = std::array<std::byte, 32>;

// The entropy seam entropy_sys.hpp exposes; a function pointer so the
// policy hands it entropy::fill and tests inject failure.
using entropy_fill = http::outcome (*)(std::span<std::byte>);

inline constexpr std::size_t k_nonce_hex_length = 112;
inline constexpr std::chrono::seconds k_future_skew{60};

enum class nonce_class : std::uint8_t {
    fresh,    // ours, unexpired, within the skew window
    expired,  // ours, past t + ttl: re-challenge with stale=TRUE
    future,   // ours, stamped beyond now + k_future_skew: refuse
    invalid,  // not ours: wrong shape or tag mismatch
};

namespace nonce_detail {

inline constexpr std::string_view k_mac_prefix = "libhttpserver-v3-nonce-v1|";

inline void store_be64(std::byte* out, std::uint64_t value) noexcept {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::byte>((value >> (56 - 8 * i)) & 0xff);
    }
}

inline std::uint64_t load_be64(const std::byte* in) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8)
              | static_cast<std::uint64_t>(
                    static_cast<unsigned char>(in[i]));
    }
    return value;
}

// hex(t) + hex(r): the nonce's public head, which the tag also covers.
inline std::string nonce_head_hex(std::uint64_t now_unix,
                                  std::span<const std::byte> entropy) {
    std::byte stamp[8];
    store_be64(stamp, now_unix);
    std::string head = encode_hex(stamp);
    head += encode_hex(entropy);
    return head;
}

// The suffix-key MAC: SHA-256 over prefix|hex(t)|hex(r)|hex(key).
inline std::string nonce_tag_hex(std::string_view head_hex,
                                 const nonce_key& key) {
    std::string mac(k_mac_prefix);
    mac += head_hex;
    mac.push_back('|');
    mac += encode_hex(key);
    const std::byte* raw = reinterpret_cast<const std::byte*>(mac.data());
    const std::array<std::byte, 32> tag =
        sha256(std::span<const std::byte>(raw, mac.size()));
    return encode_hex(tag);
}

}  // namespace nonce_detail

// Mints one nonce stamped @p now_unix under @p key. Draws 16 bytes of
// OS entropy through @p fill, retrying a failed draw exactly once;
// a persistently failing source yields std::nullopt (the caller's
// nonce_unavailable verdict).
inline std::optional<std::string> mint_nonce(const nonce_key& key,
                                             std::uint64_t now_unix,
                                             entropy_fill fill) {
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

// Classifies a presented nonce: shape and tag first (invalid is
// decided in constant time over the tag), then the expiry matrix.
inline nonce_class classify_nonce(std::string_view nonce,
                                  const nonce_key& key,
                                  std::uint64_t now_unix,
                                  std::chrono::seconds ttl) {
    if (nonce.size() != k_nonce_hex_length) return nonce_class::invalid;
    const std::optional<std::vector<std::byte>> decoded =
        decode_hex(nonce);
    if (!decoded.has_value()) return nonce_class::invalid;

    using nonce_detail::load_be64;
    using nonce_detail::nonce_tag_hex;
    const std::string head(nonce.substr(0, k_nonce_hex_length - 64));
    if (!constant_time_equal(
            nonce.substr(k_nonce_hex_length - 64),
            nonce_tag_hex(head, key))) {
        return nonce_class::invalid;
    }
    const std::uint64_t stamped = load_be64(decoded->data());
    const std::int64_t ttl_seconds =
        static_cast<std::int64_t>(ttl.count());
    if (static_cast<std::int64_t>(stamped) + ttl_seconds
        < static_cast<std::int64_t>(now_unix)) {
        return nonce_class::expired;
    }
    if (stamped > now_unix
            + static_cast<std::uint64_t>(k_future_skew.count())) {
        return nonce_class::future;
    }
    return nonce_class::fresh;
}

// Draws the per-policy MAC key from the OS entropy source; the
// outcome fails only when the source does (the caller refuses to
// construct the policy then).
inline std::optional<nonce_key> mint_key() {
    nonce_key key{};
    const std::span<std::byte> draw(key.data(), key.size());
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (entropy::fill(draw).ok()) return key;
    }
    return std::nullopt;
}

}  // namespace digest

}  // namespace detail

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_DETAIL_DIGEST_NONCE_HPP_
