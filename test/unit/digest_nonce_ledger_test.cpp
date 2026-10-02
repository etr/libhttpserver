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

// TASK-115 step 1: the Digest nonce (TASK-115 plan section 3) and its
// bounded nc ledger. The suite pins:
//   - the hex codec shared by every Digest surface: lowercase encode,
//     strict decode (odd length, non-hex, and empty all refuse);
//   - minting: 112 lowercase hex characters (56 bytes: 8-byte BE
//     timestamp, 16 bytes entropy, 32-byte keyed SHA-256 tag), unique
//     across draws, one entropy retry before giving up;
//   - classification: any tampering (wrong length, non-hex, flipped
//     digit, wrong key) is invalid; the expiry matrix around
//     t+ttl and the 60-second future-skew constant;
//   - the ledger: strictly-increasing nc admission (gaps allowed),
//     same-nc and older-nc replays, max_nc exhaustion, capacity
//     eviction (expired entries first, then smallest expiry), and
//     that reads never evict.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <httpserver/detail/digest_hex.hpp>
#include <httpserver/detail/digest_ledger.hpp>
#include <httpserver/detail/digest_nonce.hpp>
#include <httpserver/http/outcome.hpp>

#include "./littletest.hpp"

namespace {

namespace dig = httpserver::detail::digest;
namespace http = httpserver::http;

const dig::nonce_key test_key = [] {
    dig::nonce_key key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(i + 1);
    }
    return key;
}();

bool is_lower_hex(const std::string& text) {
    for (const char c : text) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower) return false;
    }
    return !text.empty();
}

// Deterministic draw for shape/uniqueness checks: every call produces
// different bytes (the call counter rides byte 0).
int draw_calls = 0;
http::outcome counting_draw(std::span<std::byte> out) {
    ++draw_calls;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>((draw_calls * 31 + 7 * i) & 0xff);
    }
    return http::outcome::okay();
}

// Fails the first g_fail_budget draws, then produces varied bytes.
int fail_budget = 0;
http::outcome flaky_draw(std::span<std::byte> out) {
    if (fail_budget > 0) {
        --fail_budget;
        return http::outcome(http::outcome_code::protocol_error,
                             "draw failed (test)");
    }
    ++draw_calls;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>((draw_calls * 53 + 11 * i) & 0xff);
    }
    return http::outcome::okay();
}

http::outcome always_fail(std::span<std::byte>) {
    return http::outcome(http::outcome_code::protocol_error,
                         "draw failed (test)");
}

}  // namespace

LT_BEGIN_SUITE(digest_nonce_ledger_suite)
    void set_up() {
        draw_calls = 0;
        fail_budget = 0;
    }
    void tear_down() { }
LT_END_SUITE(digest_nonce_ledger_suite)

LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, hex_codec_round_trips)
    const std::vector<std::byte> raw = {
        std::byte{0x00}, std::byte{0x01}, std::byte{0xfe}, std::byte{0xff},
        std::byte{0xa5}, std::byte{0x5a},
    };
    const std::string hex = dig::encode_hex(raw);
    LT_CHECK_EQ(hex, std::string("0001feffa55a"));
    const std::optional<std::vector<std::byte>> back =
        dig::decode_hex(hex);
    LT_CHECK(back.has_value());
    if (back.has_value()) LT_CHECK_EQ(back->size(), raw.size());

    LT_CHECK(!dig::decode_hex("abc").has_value());     // odd length
    LT_CHECK(!dig::decode_hex("zz").has_value());      // non-hex
    LT_CHECK(!dig::decode_hex("").has_value());        // empty
    LT_CHECK(!dig::decode_hex("0\x01").has_value());   // control byte
    // Uppercase hex decodes (clients may send it).
    const std::optional<std::vector<std::byte>> upper =
        dig::decode_hex("0A");
    LT_CHECK(upper.has_value());
    if (upper.has_value()) {
        LT_CHECK_EQ(static_cast<unsigned>((*upper)[0]), 10u);
    }
LT_END_AUTO_TEST(hex_codec_round_trips)

LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, minted_nonce_shape)
    const std::optional<std::string> minted =
        dig::mint_nonce(test_key, 1000, &counting_draw);
    LT_CHECK(minted.has_value());
    if (minted.has_value()) {
        LT_CHECK_EQ(minted->size(), std::size_t{112});
        LT_CHECK(is_lower_hex(*minted));
        // The minted timestamp is honored: fresh at t, expired at t+ttl+1.
        LT_CHECK(dig::classify_nonce(*minted, test_key, 1000,
                                     std::chrono::seconds(300))
                 == dig::nonce_class::fresh);
        LT_CHECK(dig::classify_nonce(*minted, test_key, 1301,
                                     std::chrono::seconds(300))
                 == dig::nonce_class::expired);
    }
LT_END_AUTO_TEST(minted_nonce_shape)

LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, minted_nonces_are_unique)
    const std::optional<std::string> first =
        dig::mint_nonce(test_key, 1000, &counting_draw);
    const std::optional<std::string> second =
        dig::mint_nonce(test_key, 1000, &counting_draw);
    LT_CHECK(first.has_value());
    LT_CHECK(second.has_value());
    if (first.has_value() && second.has_value()) {
        LT_CHECK(*first != *second);
    }
LT_END_AUTO_TEST(minted_nonces_are_unique)

// One entropy retry: a single failed draw still mints (two calls
// total); a permanently failing source gives up after the retry.
LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, mint_retries_entropy_once)
    fail_budget = 1;
    draw_calls = 0;
    const std::optional<std::string> recovered =
        dig::mint_nonce(test_key, 1000, &flaky_draw);
    LT_CHECK(recovered.has_value());
    LT_CHECK_EQ(draw_calls, 1);  // the second, successful draw

    const std::optional<std::string> never =
        dig::mint_nonce(test_key, 1000, &always_fail);
    LT_CHECK(!never.has_value());
LT_END_AUTO_TEST(mint_retries_entropy_once)

LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, tampering_is_invalid)
    const std::optional<std::string> minted =
        dig::mint_nonce(test_key, 1000, &counting_draw);
    LT_CHECK(minted.has_value());
    if (!minted.has_value()) return;
    const std::string good = *minted;
    const std::chrono::seconds ttl{300};

    // Flipping any digit breaks the keyed tag.
    std::string flipped = good;
    flipped[30] = (flipped[30] == '0') ? '1' : '0';
    LT_CHECK(dig::classify_nonce(flipped, test_key, 1000, ttl)
             == dig::nonce_class::invalid);
    std::string tag_bit = good;
    tag_bit[111] = (tag_bit[111] == '0') ? '1' : '0';
    LT_CHECK(dig::classify_nonce(tag_bit, test_key, 1000, ttl)
             == dig::nonce_class::invalid);
    // Wrong length, non-hex, and a foreign key all refuse.
    LT_CHECK(dig::classify_nonce(good.substr(0, 111), test_key, 1000, ttl)
             == dig::nonce_class::invalid);
    std::string non_hex = good;
    non_hex[5] = 'g';
    LT_CHECK(dig::classify_nonce(non_hex, test_key, 1000, ttl)
             == dig::nonce_class::invalid);
    dig::nonce_key other = test_key;
    other[0] = static_cast<std::byte>(0xff);
    LT_CHECK(dig::classify_nonce(good, other, 1000, ttl)
             == dig::nonce_class::invalid);
LT_END_AUTO_TEST(tampering_is_invalid)

// The expiry matrix: fresh through t+ttl inclusive, expired past it,
// fresh within the 60-second future skew, future beyond it.
LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, expiry_matrix)
    const std::optional<std::string> minted =
        dig::mint_nonce(test_key, 1000, &counting_draw);
    LT_CHECK(minted.has_value());
    if (!minted.has_value()) return;
    const std::chrono::seconds ttl{300};
    LT_CHECK(dig::classify_nonce(*minted, test_key, 1300, ttl)
             == dig::nonce_class::fresh);
    LT_CHECK(dig::classify_nonce(*minted, test_key, 1301, ttl)
             == dig::nonce_class::expired);
    // A nonce stamped far ahead of the observer is future without
    // exception; only the 60-second skew window stays fresh.
    LT_CHECK(dig::classify_nonce(*minted, test_key, 900, ttl)
             == dig::nonce_class::future);
    LT_CHECK(dig::classify_nonce(*minted, test_key, 940, ttl)
             == dig::nonce_class::fresh);
    LT_CHECK(dig::classify_nonce(*minted, test_key, 939, ttl)
             == dig::nonce_class::future);
LT_END_AUTO_TEST(expiry_matrix)

LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, nc_admission_matrix)
    dig::digest_ledger ledger(8);
    // 1 -> 2 admitted; a gap to 5 admitted; reuse of 2 replayed.
    LT_CHECK(ledger.admit("n1", 100, 500, 1,
                          dig::digest_ledger::k_no_nc_limit)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("n1", 100, 500, 2,
                          dig::digest_ledger::k_no_nc_limit)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("n1", 100, 500, 5,
                          dig::digest_ledger::k_no_nc_limit)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("n1", 100, 500, 2,
                          dig::digest_ledger::k_no_nc_limit)
             == dig::nc_admission::replayed);
    // A distinct nonce has its own counter.
    LT_CHECK(ledger.admit("n2", 100, 500, 1,
                          dig::digest_ledger::k_no_nc_limit)
             == dig::nc_admission::admitted);
    LT_CHECK_EQ(ledger.size(), std::size_t{2});

    // max_nc exhaustion: last_nc reaching the cap makes every further
    // use stale, even a strictly larger nc.
    LT_CHECK(ledger.admit("n3", 100, 500, 1, 2)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("n3", 100, 500, 2, 2)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("n3", 100, 500, 3, 2)
             == dig::nc_admission::exhausted);
    LT_CHECK(ledger.admit("n3", 100, 500, 4, 2)
             == dig::nc_admission::exhausted);
LT_END_AUTO_TEST(nc_admission_matrix)

// At capacity the ledger erases expired entries first, then the
// smallest-expiry survivor; admitting an existing nonce never evicts.
LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, capacity_evicts_expired_first)
    dig::digest_ledger ledger(2);
    const auto unlimited = dig::digest_ledger::k_no_nc_limit;
    LT_CHECK(ledger.admit("early", 50, 60, 1, unlimited)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("late", 50, 900, 1, unlimited)
             == dig::nc_admission::admitted);
    // "early" is expired at now=100: its slot goes first even though
    // the new arrival expires sooner than "late".
    LT_CHECK(ledger.admit("sooner", 100, 200, 1, unlimited)
             == dig::nc_admission::admitted);
    LT_CHECK_EQ(ledger.size(), std::size_t{2});
    // "late" survived: its nc counter still stands.
    LT_CHECK(ledger.admit("late", 100, 900, 1, unlimited)
             == dig::nc_admission::replayed);
    // Nothing expired at now=150: the smallest expiry ("sooner", 200)
    // is evicted for the newcomer.
    LT_CHECK(ledger.admit("newcomer", 150, 800, 1, unlimited)
             == dig::nc_admission::admitted);
    LT_CHECK_EQ(ledger.size(), std::size_t{2});
    LT_CHECK(ledger.admit("sooner", 150, 200, 1, unlimited)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("late", 150, 900, 2, unlimited)
             == dig::nc_admission::admitted);
LT_END_AUTO_TEST(capacity_evicts_expired_first)

// An admission against an existing nonce is a read-modify-write, not
// an insertion: a full ledger still serves it without evicting peers.
LT_BEGIN_AUTO_TEST(digest_nonce_ledger_suite, reads_never_evict)
    dig::digest_ledger ledger(2);
    const auto unlimited = dig::digest_ledger::k_no_nc_limit;
    LT_CHECK(ledger.admit("a", 100, 900, 1, unlimited)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("b", 100, 900, 1, unlimited)
             == dig::nc_admission::admitted);
    LT_CHECK(ledger.admit("a", 100, 900, 2, unlimited)
             == dig::nc_admission::admitted);
    LT_CHECK_EQ(ledger.size(), std::size_t{2});
    LT_CHECK(ledger.admit("b", 100, 900, 1, unlimited)
             == dig::nc_admission::replayed);
LT_END_AUTO_TEST(reads_never_evict)

LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
