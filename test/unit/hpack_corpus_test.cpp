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

#include <httpserver/detail/hpack_connection.hpp>
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
LT_BEGIN_AUTO_TEST(hpack_corpus_suite, complete_rfc_sections_and_table_snapshots)
    using httpserver::detail::hpack_decoder;
    using httpserver::detail::hpack_section_limits;
    const hpack_section_limits limits{4096, 4096, 32};
    const auto scope = httpserver::server::resource_budget::root({});
    const auto check = [&](hpack_decoder& decoder, std::size_t block, const auto& fields, const auto& table, std::size_t bytes) {
        const auto decoded = decoder.decode(octets(unhex(hpack_fixture::blocks[block])), limits);
        LT_CHECK(decoded.status.ok() && decoded.fields.size() == fields.size());
        for (std::size_t i = 0; i < fields.size(); ++i) {
            LT_CHECK(decoded.fields[i].name == fields[i].first && decoded.fields[i].value == fields[i].second);
        }
        LT_CHECK(decoder.table().size() == table.size() && decoder.table().bytes() == bytes);
        for (std::size_t i = 0; i < table.size(); ++i) {
            const auto entry = decoder.table().lookup(i + 62);
            LT_CHECK(entry && entry->name == table[i].first && entry->value == table[i].second);
        }
    };
    using list = std::vector<std::pair<std::string_view, std::string_view>>;
    const list none;
    const list custom{{"custom-key", "custom-header"}};
    for (std::size_t block = 0; block < 4; ++block) {
        hpack_decoder decoder(scope);
        const list fields = block == 0 ? custom : block == 1 ? list{{":path", "/sample/path"}} :
            block == 2 ? list{{"password", "secret"}} : list{{":method", "GET"}};
        check(decoder, block, fields, block == 0 ? custom : none, block == 0 ? 55 : 0);
    }
    const list authority{{":authority", "www.example.com"}};
    const list cache{{"cache-control", "no-cache"}, {":authority", "www.example.com"}};
    const list added{{"custom-key", "custom-value"}, {"cache-control", "no-cache"}, {":authority", "www.example.com"}};
    for (std::size_t start : {4, 7}) {
        hpack_decoder decoder(scope);
        list fields{{":method", "GET"}, {":scheme", "http"}, {":path", "/"}, {":authority", "www.example.com"}};
        check(decoder, start, fields, authority, 57);
        fields.emplace_back("cache-control", "no-cache");
        check(decoder, start + 1, fields, cache, 110);
        fields = {{":method", "GET"}, {":scheme", "https"}, {":path", "/index.html"}, {":authority", "www.example.com"}, {"custom-key", "custom-value"}};
        check(decoder, start + 2, fields, added, 164);
    }
    const auto date1 = "Mon, 21 Oct 2013 20:13:21 GMT"sv;
    const auto date2 = "Mon, 21 Oct 2013 20:13:22 GMT"sv;
    const auto location = "https://www.example.com"sv;
    const auto cookie = "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"sv;
    for (std::size_t start : {10, 13}) {
        hpack_decoder decoder(scope);
        LT_CHECK(decoder.acknowledge_maximum(256).ok());
        // Establish the examples' negotiated table size before replaying them.
        LT_CHECK(decoder.decode(octets(unhex("3fe101")), limits).status.ok());
        list fields{{":status", "302"}, {"cache-control", "private"}, {"date", date1}, {"location", location}};
        list table{{"location", location}, {"date", date1}, {"cache-control", "private"}, {":status", "302"}};
        check(decoder, start, fields, table, 222);
        fields[0].second = "307";
        table = {{":status", "307"}, {"location", location}, {"date", date1}, {"cache-control", "private"}};
        check(decoder, start + 1, fields, table, 222);
        fields = {{":status", "200"}, {"cache-control", "private"}, {"date", date2}, {"location", location}, {"content-encoding", "gzip"}, {"set-cookie", cookie}};
        table = {{"set-cookie", cookie}, {"content-encoding", "gzip"}, {"date", date2}};
        check(decoder, start + 2, fields, table, 215);
    }
LT_END_AUTO_TEST(complete_rfc_sections_and_table_snapshots)
LT_BEGIN_AUTO_TEST(hpack_corpus_suite, bounded_stateful_section_mutations)
    for (const auto fixture : hpack_fixture::blocks) {
        const auto original = unhex(fixture);
        hpack_fuzz_sections(octets(original));
        for (std::size_t i = 0; i < original.size(); ++i) {
            auto mutation = original;
            for (const unsigned tag : {0x00, 0x10, 0x20, 0x40, 0x80, 0xff}) {
                mutation[i] = static_cast<char>(tag);
                hpack_fuzz_sections(octets(mutation));
            }
            hpack_fuzz_sections(octets(original).first(i));
        }
    }
LT_END_AUTO_TEST(bounded_stateful_section_mutations)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
