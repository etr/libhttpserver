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
     License along with this library; if not, write to the file
     LICENSE in the distribution; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
     02110-1301 USA
*/

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/detail/hpack_primitives.hpp>
#include <httpserver/detail/hpack_static_table.hpp>
#include "../data/hpack/rfc7541.hpp"
#include "../fuzz/hpack_fuzz.hpp"
#include "./littletest.hpp"

using std::string_view_literals::operator""sv;

namespace {
using httpserver::detail::hpack_decode_huffman;
using httpserver::detail::hpack_decode_integer;
using httpserver::detail::hpack_decode_string;
using httpserver::detail::hpack_encode_huffman;
using httpserver::detail::hpack_encode_string;
using httpserver::detail::hpack_state;
using httpserver::detail::hpack_static_lookup;
std::span<const std::uint8_t> octets(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}
std::string unhex(std::string_view s) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2) out.push_back(static_cast<char>(16 * digits.find(s[i]) + digits.find(s[i + 1])));
    return out;
}
}  // namespace
LT_BEGIN_SUITE(hpack_corpus_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hpack_corpus_suite)
LT_BEGIN_AUTO_TEST(hpack_corpus_suite, rfc_primitive_constituents)
    for (const auto& fixture : hpack_fixture::literals) {
        const auto wire = unhex(hpack_fixture::blocks[fixture.block]);
        LT_CHECK(fixture.offset + fixture.wire_size <= wire.size());
        const auto input = octets(wire).subspan(fixture.offset);
        const auto decoded = hpack_decode_string(input, {128, 128});
        LT_CHECK(decoded.status.ok());
        LT_CHECK(decoded.value == fixture.text);
        LT_CHECK(decoded.consumed == fixture.wire_size);
        const bool huffman = (input[0] & 0x80) != 0;
        const auto encoded = hpack_encode_string(octets(fixture.text), huffman, {128, 128}, 128);
        LT_CHECK(encoded.status.ok());
        LT_CHECK(encoded.value == wire.substr(fixture.offset, fixture.wire_size));
        LT_CHECK(hpack_decode_string(input.first(fixture.wire_size - 1), {128, 128}).status.state == hpack_state::incomplete);
        if (huffman) {
            const auto payload = input.subspan(1, fixture.wire_size - 1);
            LT_CHECK(hpack_decode_huffman(payload, {128, 128}).value == fixture.text);
            LT_CHECK(hpack_encode_huffman(octets(fixture.text), {128, 128}, 128).value == wire.substr(fixture.offset + 1, fixture.wire_size - 1));
        }
    }
    for (const auto& fixture : hpack_fixture::integers) {
        const auto wire = unhex(hpack_fixture::blocks[fixture.block]);
        const auto decoded = hpack_decode_integer(octets(wire).subspan(fixture.offset), fixture.prefix, {});
        LT_CHECK(decoded.status.ok() && decoded.value == fixture.value && decoded.consumed == 1);
        // Dynamic references are checked as integer constituents only.
        if (fixture.value > 61) LT_CHECK(hpack_static_lookup(fixture.value) == nullptr);
    }
LT_END_AUTO_TEST(rfc_primitive_constituents)
LT_BEGIN_AUTO_TEST(hpack_corpus_suite, ordered_duplicate_append_seam)
    std::vector<std::pair<std::string, std::string>> fields;
    const auto wire = unhex("8207616c7068613d318207616c7068613d32");
    std::size_t pos = 0;
    while (pos < wire.size()) {
        const auto index = hpack_decode_integer(octets(wire).subspan(pos), 7, {});
        LT_CHECK(index.status.ok());
        const auto* entry = hpack_static_lookup(index.value);
        LT_CHECK(entry != nullptr);
        fields.emplace_back(entry->name, entry->value);
        pos += index.consumed;
        const auto value = hpack_decode_string(octets(wire).subspan(pos), {16, 16});
        LT_CHECK(value.status.ok());
        fields.emplace_back("cookie", value.value);
        pos += value.consumed;
    }
    LT_CHECK(fields.size() == 4);
    LT_CHECK(fields[0] == std::make_pair(std::string(":method"), std::string("GET")));
    LT_CHECK(fields[1] == std::make_pair(std::string("cookie"), std::string("alpha=1")));
    LT_CHECK(fields[2] == fields[0]);
    LT_CHECK(fields[3] == std::make_pair(std::string("cookie"), std::string("alpha=2")));
LT_END_AUTO_TEST(ordered_duplicate_append_seam)
LT_BEGIN_AUTO_TEST(hpack_corpus_suite, deterministic_seeded_malformed_mutations)
    std::uint32_t state = 0x7541137;
    for (const auto fixture : hpack_fixture::blocks) {
        const auto original = unhex(fixture);
        for (std::size_t iteration = 0; iteration < 512; ++iteration) {
            std::string mutated(8, '\0');
            mutated += original;
            for (int j = 0; j < 4; ++j) {
                state = state * 1664525U + 1013904223U;
                const auto pos = state % mutated.size();
                mutated[pos] = static_cast<char>(state >> 24);
            }
            if ((iteration % 4) == 0) mutated.resize(iteration % mutated.size());
            hpack_fuzz_input(octets(mutated));
        }
    }
    for (unsigned prefix = 0; prefix <= 10; ++prefix) {
        for (const auto payload : {""sv, "\xff"sv, "\xff\xff\xff\xff"sv, "\x7f\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\0"sv}) {
            std::string seed(8, '\xff');
            seed[0] = static_cast<char>(prefix);
            seed += payload;
            hpack_fuzz_input(octets(seed));
        }
    }
LT_END_AUTO_TEST(deterministic_seeded_malformed_mutations)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
