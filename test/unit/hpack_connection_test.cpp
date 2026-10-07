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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <httpserver/detail/hpack_connection.hpp>
#include <httpserver/detail/hpack_decoder.hpp>
#include "./littletest.hpp"
using std::string_view_literals::operator""sv;
namespace allocation_observer {
bool enabled = false;
std::size_t max_requested = 0;
}
void* operator new(std::size_t size) {
    if (allocation_observer::enabled) allocation_observer::max_requested = std::max(allocation_observer::max_requested, size);
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
namespace {
namespace server = httpserver::server;
namespace http = httpserver::http;
using httpserver::detail::hpack_connection;
using httpserver::detail::hpack_decoder;
using httpserver::detail::hpack_encoder;
using httpserver::detail::hpack_section_limits;
using httpserver::detail::hpack_field_view;
using httpserver::detail::hpack_indexing;
using httpserver::detail::hpack_dynamic_table;
using httpserver::detail::hpack_encode_string;
using httpserver::detail::hpack_state;
server::resource_budget budget() { return server::resource_budget::root({}); }
std::span<const std::uint8_t> octets(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}
constexpr hpack_section_limits limits{4096, 4096, 64};
}  // namespace
LT_BEGIN_SUITE(hpack_connection_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(hpack_connection_suite)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, acknowledged_decoder_capacity_and_required_shrink)
    hpack_decoder decoder(budget());
    LT_CHECK(decoder.table().capacity() == 4096 && decoder.acknowledged_maximum() == 4096);
    LT_CHECK(decoder.decode(octets("\x40\x01x\x01y"sv), limits).status.ok());
    LT_CHECK(decoder.acknowledge_maximum(0).ok());
    LT_CHECK(decoder.acknowledge_maximum(4096).ok());
    LT_CHECK(decoder.decode(octets("\x20\x3f\xe1\x1f"sv), limits).status.ok());
    LT_CHECK(decoder.table().capacity() == 4096 && decoder.table().size() == 0);
    LT_CHECK(!decoder.decode(octets("\xbe"sv), limits).status.ok());
    for (auto wire : {""sv, "\x82"sv, "\x3f\x01\x82"sv}) {
        hpack_decoder missing(budget());
        LT_CHECK(missing.acknowledge_maximum(0).ok());
        LT_CHECK(!missing.decode(octets(wire), limits).status.ok() && !missing.usable());
    }
    hpack_decoder raised(budget());
    LT_CHECK(raised.acknowledge_maximum(5000).ok());
    LT_CHECK(raised.decode(octets("\x3f\xe2\x1f"sv), limits).status.ok());
    LT_CHECK(raised.table().capacity() == 4097);
    LT_CHECK(raised.acknowledge_maximum(UINT64_MAX).state == hpack_state::invalid_argument);
    LT_CHECK(raised.usable() && raised.acknowledged_maximum() == 5000);
LT_END_AUTO_TEST(acknowledged_decoder_capacity_and_required_shrink)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, pending_encoder_minimum_and_final_updates)
    hpack_encoder encoder(budget());
    LT_CHECK(encoder.table().capacity() == 4096 && encoder.peer_maximum() == 4096);
    LT_CHECK(encoder.choose_capacity(5000).state == hpack_state::invalid_argument);
    LT_CHECK(encoder.set_peer_maximum(100).ok());
    LT_CHECK(encoder.choose_capacity(0).ok());
    LT_CHECK(encoder.set_peer_maximum(4096).ok());
    LT_CHECK(encoder.choose_capacity(64).ok());
    const auto block = encoder.encode({}, limits);
    LT_CHECK(block.status.ok() && block.value == "\x20\x3f\x21"sv);
    LT_CHECK(encoder.encode({}, limits).value.empty());
    LT_CHECK(encoder.table().capacity() == 64);
    LT_CHECK(encoder.choose_capacity(UINT64_MAX).state == hpack_state::invalid_argument);
    LT_CHECK(encoder.usable());
LT_END_AUTO_TEST(pending_encoder_minimum_and_final_updates)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, connection_and_direction_isolation_in_wire_order)
    auto scope = budget();
    hpack_connection a(scope), b(scope);
    const auto insert = "\x40\x01x\x01y"sv;
    LT_CHECK(a.decoder().decode(octets(insert), limits).status.ok());
    LT_CHECK(a.decoder().decode(octets("\xbe"sv), limits).fields[0].value == "y");
    LT_CHECK(!b.decoder().decode(octets("\xbe"sv), limits).status.ok());
    LT_CHECK(a.encoder().table().size() == 0);
    const std::array<hpack_field_view, 1> field{{{"x", "y"}}};
    LT_CHECK(a.encoder().encode(field, limits).value == insert);
    LT_CHECK(a.encoder().encode(field, limits).value == "\xbe"sv);
    LT_CHECK(a.decoder().table().size() == 1 && a.encoder().table().size() == 1);
    LT_CHECK(a.encoder().set_peer_maximum(0).ok());
    LT_CHECK(a.decoder().table().capacity() == 4096);
    LT_CHECK(a.decoder().acknowledge_maximum(64).ok());
    LT_CHECK(a.encoder().peer_maximum() == 0);
LT_END_AUTO_TEST(connection_and_direction_isolation_in_wire_order)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, independent_section_limits_exact_and_one_over)
    const auto insert = "\x40\x01x\x01y"sv;
    const std::array<hpack_field_view, 1> field{{{"x", "y"}}};
    for (const hpack_section_limits cap : {hpack_section_limits{5, 34, 1}, {4, 34, 1}, {5, 33, 1}, {5, 34, 0}}) {
        auto scope = budget();
        hpack_connection connection(scope);
        const bool fits = cap.max_compressed_bytes == 5 && cap.max_expanded_bytes == 34 && cap.max_fields == 1;
        const auto decoded = connection.decoder().decode(octets(insert), cap);
        const auto encoded = connection.encoder().encode(field, cap);
        LT_CHECK(decoded.status.ok() == fits && encoded.status.ok() == fits);
        LT_CHECK(fits || (decoded.fields.empty() && encoded.value.empty() && encoded.consumed == 0));
        LT_CHECK(fits || (!connection.decoder().usable() && !connection.encoder().usable()));
        LT_CHECK(fits || scope.in_use(server::resource::hpack_table_bytes) == 0);
    }
    hpack_decoder repeated(budget());
    LT_CHECK(repeated.decode(octets(insert), limits).status.ok());
    LT_CHECK(repeated.decode(octets("\xbe\xbe"sv), {2, 68, 2}).status.ok());
    const auto over = repeated.decode(octets("\xbe\xbe"sv), {2, 67, 2});
    LT_CHECK(over.status.state == hpack_state::limit_exceeded && over.fields.empty());
    const auto compressed = "\x41\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff"sv;
    hpack_decoder exact(budget()), expanded(budget());
    LT_CHECK(exact.decode(octets(compressed), {14, 57, 1}).status.ok());
    LT_CHECK(expanded.decode(octets(compressed), {14, 56, 1}).status.state == hpack_state::limit_exceeded);
    hpack_decoder capacity(budget());
    LT_CHECK(capacity.decode(octets("\x20"sv), {1, 0, 0}).status.ok());
    LT_CHECK(capacity.decode(octets(insert), {5, 34, 1}).status.ok());
    LT_CHECK(capacity.table().size() == 0 && capacity.table().capacity() == 0);
LT_END_AUTO_TEST(independent_section_limits_exact_and_one_over)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, ancestor_exhaustion_failure_and_destruction_release)
    server::budget_limits cap;
    cap.set(server::resource::hpack_table_bytes, 68);
    auto root = server::resource_budget::root(cap);
    server::resource_budget child;
    LT_CHECK(root.child(cap, child).ok());
    {
        hpack_connection connection(child);
        LT_CHECK(connection.decoder().decode(octets("\x40\x01x\x01y"sv), limits).status.ok());
        const std::array<hpack_field_view, 1> first{{{"a", "b"}}};
        LT_CHECK(connection.encoder().encode(first, limits).status.ok());
        LT_CHECK(root.in_use(server::resource::hpack_table_bytes) == 68);
        const std::array<hpack_field_view, 1> refused{{{"c", "d"}}};
        const auto out = connection.encoder().encode(refused, limits);
        LT_CHECK(out.status.state == hpack_state::limit_exceeded && out.value.empty());
        LT_CHECK(root.in_use(server::resource::hpack_table_bytes) == 34);
        LT_CHECK(child.in_use(server::resource::hpack_table_bytes) == 34);
        LT_CHECK(!connection.encoder().encode(first, limits).status.ok());
        LT_CHECK(connection.decoder().decode(octets("\xbe"sv), limits).status.ok());
        LT_CHECK(connection.decoder().decode(octets("\x20"sv), limits).status.ok());
        LT_CHECK(root.in_use(server::resource::hpack_table_bytes) == 0);
        LT_CHECK(connection.decoder().decode(octets("\x3f\xe1\x1f\x40\x01x\x01y"sv), limits).status.ok());
    }
    LT_CHECK(root.in_use(server::resource::hpack_table_bytes) == 0);
LT_END_AUTO_TEST(ancestor_exhaustion_failure_and_destruction_release)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, rejection_precedes_input_sized_growth)
    const std::string large(65536, 'x');
    const auto name = hpack_encode_string(octets(large), false, {large.size(), large.size()}, large.size() + 8).value;
    const auto value = hpack_encode_string(octets(large), true, {large.size(), large.size()}, large.size() + 8).value;
    const std::string wire = std::string("\x00", 1) + name + "\x01y";
    const std::string huffman = std::string("\x00\x01x", 3) + value;
    for (const auto& input : {wire, huffman}) {
        hpack_decoder decoder(budget());
        allocation_observer::max_requested = 0;
        allocation_observer::enabled = true;
        const auto result = decoder.decode(octets(input), {input.size(), 65568, 1});
        allocation_observer::enabled = false;
        LT_CHECK(result.status.state == hpack_state::limit_exceeded && result.fields.empty());
        LT_CHECK(allocation_observer::max_requested < large.size());
    }
    hpack_encoder encoder(budget());
    const std::array<hpack_field_view, 1> fields{{{"x", large}}};
    allocation_observer::max_requested = 0;
    allocation_observer::enabled = true;
    const auto result = encoder.encode(fields, {65536, 64, 1});
    allocation_observer::enabled = false;
    LT_CHECK(result.status.state == hpack_state::limit_exceeded && result.value.empty());
    LT_CHECK(allocation_observer::max_requested < large.size());
LT_END_AUTO_TEST(rejection_precedes_input_sized_growth)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, table_admission_precedes_owned_octet_copies)
    server::budget_limits cap;
    cap.set(server::resource::hpack_table_bytes, 1);
    auto scope = server::resource_budget::root(cap);
    hpack_dynamic_table table(scope);
    const std::string value(4000, 'x');
    allocation_observer::max_requested = 0;
    allocation_observer::enabled = true;
    const auto refused = table.insert_copy("x", value);
    allocation_observer::enabled = false;
    LT_CHECK(refused.state == hpack_state::limit_exceeded);
    LT_CHECK(table.size() == 0 && allocation_observer::max_requested < value.size());
    const std::string oversized(8192, 'x');
    allocation_observer::max_requested = 0;
    allocation_observer::enabled = true;
    const auto cleared = table.insert_copy("x", oversized);
    allocation_observer::enabled = false;
    LT_CHECK(cleared.ok() && table.size() == 0 && allocation_observer::max_requested == 0);
LT_END_AUTO_TEST(table_admission_precedes_owned_octet_copies)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, partial_block_failure_releases_state_and_hides_outputs)
    auto scope = budget();
    hpack_connection connection(scope);
    const auto incoming = connection.decoder().decode(octets("\x40\x01x\x01y\x80"sv), limits);
    LT_CHECK(incoming.status.state == hpack_state::malformed && incoming.fields.empty());
    LT_CHECK(!connection.decoder().usable() && scope.in_use(server::resource::hpack_table_bytes) == 0);
    const std::array<hpack_field_view, 2> fields{{{"x", "y"}, {"z", "w"}}};
    const auto outgoing = connection.encoder().encode(fields, {6, 68, 2});
    LT_CHECK(outgoing.status.state == hpack_state::limit_exceeded && outgoing.value.empty());
    LT_CHECK(outgoing.consumed == 0 && !connection.encoder().usable());
    LT_CHECK(scope.in_use(server::resource::hpack_table_bytes) == 0);
    hpack_decoder repeated(budget());
    LT_CHECK(repeated.decode(octets("\x40\x01x\x01y"sv), limits).status.ok());
    LT_CHECK(repeated.decode(octets("\xbe\xbe"sv), {2, 68, 1}).status.state == hpack_state::limit_exceeded);
LT_END_AUTO_TEST(partial_block_failure_releases_state_and_hides_outputs)
LT_BEGIN_AUTO_TEST(hpack_connection_suite, semantic_field_count_admission_precedes_cache_growth)
    http::fields fields;
    for (std::size_t i = 0; i < 1024; ++i) fields.append("x", "y");
    hpack_encoder encoder(budget());
    allocation_observer::max_requested = 0;
    allocation_observer::enabled = true;
    const auto result = encoder.encode_fields(fields, hpack_indexing::incremental, {64, 64, 0});
    allocation_observer::enabled = false;
    LT_CHECK(result.status.state == hpack_state::limit_exceeded && result.value.empty());
    LT_CHECK(allocation_observer::max_requested == 0);
LT_END_AUTO_TEST(semantic_field_count_admission_precedes_cache_growth)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
