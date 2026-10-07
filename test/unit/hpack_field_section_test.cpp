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

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <httpserver/detail/hpack_decoder.hpp>
#include <httpserver/detail/hpack_encoder.hpp>
#include "./littletest.hpp"
using std::string_view_literals::operator""sv;
namespace {
namespace server = httpserver::server;
namespace http = httpserver::http;
using httpserver::detail::hpack_decoder;
using httpserver::detail::hpack_encoder;
using httpserver::detail::hpack_section_limits;
using httpserver::detail::hpack_field_view;
using httpserver::detail::hpack_indexing;
using httpserver::detail::hpack_state;
server::resource_budget budget() { return server::resource_budget::root({}); }
std::span<const std::uint8_t> octets(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}
constexpr hpack_section_limits limits{4096, 4096, 64};
}  // namespace
LT_BEGIN_SUITE(hpack_field_section_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hpack_field_section_suite)
LT_BEGIN_AUTO_TEST(hpack_field_section_suite, representations_order_and_cross_block_references)
    hpack_decoder decoder(budget());
    auto first = decoder.decode(octets("\x82\x40\x06" "cookie\x03" "a=1\xbe\x0f\x11\x03" "a=2\x1f\x11\x03" "a=3"sv), limits);
    LT_CHECK(first.status.ok() && first.fields.size() == 5);
    LT_CHECK(first.fields[1].name == "cookie" && first.fields[2].value == "a=1");
    LT_CHECK(first.fields[3].indexing == hpack_indexing::without_indexing);
    LT_CHECK(first.fields[4].indexing == hpack_indexing::never_indexed);
    const auto semantic = first.materialize();
    LT_CHECK(semantic.entries()[0].name == ":method");
    LT_CHECK(semantic.first("cookie") == "a=1" && semantic.all("cookie").size() == 4);
    LT_CHECK(semantic.all("cookie")[3] == "a=3");
    const auto second = decoder.decode(octets("\xbe"sv), limits);
    LT_CHECK(second.status.ok() && second.fields[0].value == "a=1");
    const auto huffman = decoder.decode(octets("\x41\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff"sv), limits);
    LT_CHECK(huffman.status.ok() && huffman.fields[0].value == "www.example.com");
LT_END_AUTO_TEST(representations_order_and_cross_block_references)
LT_BEGIN_AUTO_TEST(hpack_field_section_suite, malformed_complete_blocks_poison_and_publish_nothing)
    for (auto wire : {"\x80"sv, "\xbe"sv, "\xff"sv, "\x40"sv, "\x00\x03" "ab"sv,
                      "\x00\x01x\x81\xff"sv, "\x82\x20"sv, "\x3f\xe2\x1f"sv}) {
        auto scope = budget();
        hpack_decoder decoder(scope);
        LT_CHECK(decoder.decode(octets("\x40\x01x\x01y"sv), limits).status.ok());
        // Index 63 remains unavailable even after one insertion.
        const auto bad = wire == "\xbe"sv ? "\xbf"sv : wire;
        const auto result = decoder.decode(octets(bad), limits);
        LT_CHECK(!result.status.ok() && result.fields.empty());
        LT_CHECK(result.status.state != hpack_state::incomplete);
        LT_CHECK(!decoder.usable() && scope.in_use(server::resource::hpack_table_bytes) == 0);
        LT_CHECK(!decoder.decode(octets("\x82"sv), limits).status.ok());
    }
LT_END_AUTO_TEST(malformed_complete_blocks_poison_and_publish_nothing)
LT_BEGIN_AUTO_TEST(hpack_field_section_suite, encoder_fixed_wire_order_and_dynamic_policy)
    hpack_encoder encoder(budget());
    const std::array<hpack_field_view, 5> fields{{
        {":method", "GET"}, {"x", "y"}, {"x", "y"},
        {"x", "y", hpack_indexing::never_indexed}, {"cookie", "a=1", hpack_indexing::without_indexing}}};
    const auto block = encoder.encode(fields, limits);
    LT_CHECK(block.status.ok());
    LT_CHECK(block.value == "\x82\x40\x01x\x01y\xbe\x1f\x2f\x01y\x0f\x11\x03" "a=1"sv);
    LT_CHECK(encoder.table().size() == 1);
    hpack_decoder decoder(budget());
    const auto decoded = decoder.decode(octets(block.value), limits);
    LT_CHECK(decoded.status.ok() && decoded.fields[3].indexing == hpack_indexing::never_indexed);
    hpack_encoder relay(budget());
    const auto forwarded = relay.encode_section(decoded.fields, limits);
    LT_CHECK(forwarded.status.ok() && forwarded.value == block.value);
    const std::array<hpack_field_view, 1> dynamic{{{"x", "y"}}};
    LT_CHECK(encoder.encode(dynamic, limits).value == "\xbe"sv);
    const std::array<hpack_field_view, 1> literal{{{"x", "z"}}};
    LT_CHECK(encoder.encode(literal, limits).value == "\x7e\x01z"sv);
    const std::array<hpack_field_view, 1> invalid{{{"x", "y", static_cast<hpack_indexing>(99)}}};
    LT_CHECK(encoder.encode(invalid, limits).status.state == hpack_state::invalid_argument);
    LT_CHECK(encoder.usable() && encoder.table().size() == 2);
LT_END_AUTO_TEST(encoder_fixed_wire_order_and_dynamic_policy)
LT_BEGIN_AUTO_TEST(hpack_field_section_suite, fixed_huffman_wire_and_semantic_adapter)
    hpack_encoder encoder(budget());
    http::fields fields;
    fields.append(":method", "GET"); fields.append(":scheme", "http"); fields.append(":path", "/");
    fields.append(":authority", "www.example.com");
    const auto huffman = encoder.encode_fields(fields, hpack_indexing::incremental, limits, true);
    LT_CHECK(huffman.status.ok());
    LT_CHECK(huffman.value == "\x82\x86\x84\x41\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff"sv);
    hpack_encoder raw(budget());
    LT_CHECK(raw.encode_fields(fields, hpack_indexing::incremental, limits).value == "\x82\x86\x84\x41\x0fwww.example.com"sv);
    http::fields duplicates;
    duplicates.append("cookie", "a=1"); duplicates.append("cookie", "a=2");
    const auto duplicate_wire = raw.encode_fields(duplicates, hpack_indexing::never_indexed, limits);
    LT_CHECK(duplicate_wire.value == "\x1f\x11\x03" "a=1\x1f\x11\x03" "a=2"sv);
LT_END_AUTO_TEST(fixed_huffman_wire_and_semantic_adapter)
LT_BEGIN_AUTO_TEST(hpack_field_section_suite, dynamic_name_survives_insertion_eviction)
    hpack_decoder decoder(budget());
    const auto wire = "\x3f\x03\x40\x01x\x01y\x7e\x01z\xbe"sv;
    const auto decoded = decoder.decode(octets(wire), limits);
    LT_CHECK(decoded.status.ok() && decoded.fields.size() == 3);
    LT_CHECK(decoded.fields[0].name == "x" && decoded.fields[0].value == "y");
    LT_CHECK(decoded.fields[1].name == "x" && decoded.fields[1].value == "z");
    LT_CHECK(decoded.fields[2].name == "x" && decoded.fields[2].value == "z");
    LT_CHECK(decoder.table().bytes() == 34 && decoder.table().size() == 1);
    const std::array<hpack_field_view, 3> fields{{{"x", "y"}, {"x", "z"}, {"x", "z"}}};
    hpack_encoder encoder(budget());
    LT_CHECK(encoder.choose_capacity(34).ok());
    LT_CHECK(encoder.encode(fields, limits).value == wire);
LT_END_AUTO_TEST(dynamic_name_survives_insertion_eviction)
LT_BEGIN_AUTO_TEST(hpack_field_section_suite, invalid_policy_preflight_preserves_pending_update)
    hpack_encoder encoder(budget());
    LT_CHECK(encoder.choose_capacity(0).ok());
    const std::array<hpack_field_view, 2> fields{{{"x", "y"}, {"x", "y", static_cast<hpack_indexing>(99)}}};
    const auto refused = encoder.encode(fields, limits);
    LT_CHECK(refused.status.state == hpack_state::invalid_argument && refused.value.empty());
    LT_CHECK(encoder.usable() && encoder.table().size() == 0);
    LT_CHECK(encoder.encode({}, limits).value == "\x20"sv);
LT_END_AUTO_TEST(invalid_policy_preflight_preserves_pending_update)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
