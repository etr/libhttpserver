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
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <httpserver/detail/qpack_decoder.hpp>
#include <httpserver/detail/qpack_encoder.hpp>
#include "data/qpack/rfc9204.hpp"
#include "./littletest.hpp"
using httpserver::detail::qpack_decoder;
using httpserver::detail::qpack_encoder;
using httpserver::detail::qpack_field;
using httpserver::detail::qpack_field_view;
using httpserver::detail::qpack_octets;
using httpserver::detail::qpack_section_limits;
using httpserver::detail::qpack_section_result;
using httpserver::detail::qpack_state;
using std::string_view_literals::operator""sv;
namespace allocation_observer {
bool enabled = false;
bool fail = false;
const std::length_error* length_failure = nullptr;
std::size_t calls = 0;
}
void* operator new(std::size_t size) {
    if (allocation_observer::enabled) {
        ++allocation_observer::calls;
        if (allocation_observer::fail) throw std::bad_alloc();
        if (allocation_observer::length_failure) throw *allocation_observer::length_failure;
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
namespace {
constexpr qpack_section_limits limits{4096, 8192, 100};
std::string unhex(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        out += static_cast<char>((digit(text[i]) << 4) | digit(text[i + 1]));
    }
    return out;
}
bool same(std::span<const qpack_field> owned, std::span<const qpack_field_view> views) {
    if (owned.size() != views.size()) return false;
    for (std::size_t i = 0; i < owned.size(); ++i) {
        if (owned[i].name != views[i].name || owned[i].value != views[i].value || owned[i].never_indexed != views[i].never_indexed) return false;
    }
    return true;
}
}  // namespace
LT_BEGIN_SUITE(qpack_section_suite)
    void set_up() {}
    void tear_down() {
        allocation_observer::enabled = false; allocation_observer::fail = false; allocation_observer::length_failure = nullptr;
    }
LT_END_SUITE(qpack_section_suite)
LT_BEGIN_AUTO_TEST(qpack_section_suite, rfc_vectors_and_all_static_indexes)
    qpack_decoder decoder;
    qpack_encoder encoder;
    const std::array path = {qpack_field_view{":path", "/index.html"}};
    const auto wire = unhex(qpack_fixture::literal_path);
    LT_CHECK(same(decoder.decode(qpack_octets(wire), limits).fields, path));
    LT_CHECK(encoder.encode(path, limits).value == wire);
    const std::array request = {qpack_field_view{":method", "GET"}, qpack_field_view{":scheme", "https"}, qpack_field_view{":path", "/"}};
    LT_CHECK(same(decoder.decode(qpack_octets(unhex(qpack_fixture::static_request)), limits).fields, request));
    LT_CHECK(encoder.encode(request, limits).value == unhex(qpack_fixture::static_request));
    for (std::size_t i = 0; i < 99; ++i) {
        // Wire constructed independently: 6-bit index, all indexes need <=2 octets.
        std::string indexed("\0\0", 2);
        indexed += static_cast<char>(0xc0 | (i < 63 ? i : 63));
        if (i >= 63) indexed += static_cast<char>(i - 63);
        const std::array field = {qpack_field_view{qpack_fixture::entries[i].name, qpack_fixture::entries[i].value}};
        LT_CHECK(same(decoder.decode(qpack_octets(indexed), limits).fields, field));
        LT_CHECK(encoder.encode(field, limits).value == indexed);
    }
    LT_CHECK(encoder.encode({}, {2, 0, 0}).value == "\0\0"sv);
    LT_CHECK(decoder.decode(qpack_octets("\0\0"sv), {2, 0, 0}).status.ok());
    LT_CHECK(decoder.decode(qpack_octets("\0\x01\xd1"sv), limits).status.ok());
    LT_CHECK(decoder.decode(qpack_octets("\0\x7f\x00\xd1"sv), limits).status.ok());
LT_END_AUTO_TEST(rfc_vectors_and_all_static_indexes)
LT_BEGIN_AUTO_TEST(qpack_section_suite, literals_sensitivity_order_and_owned_storage)
    qpack_decoder decoder;
    qpack_encoder encoder;
    const std::array fields = {qpack_field_view{":authority", "", true}, qpack_field_view{":method", "GET", true},
        qpack_field_view{"cookie", "a=1"}, qpack_field_view{"cookie", "b=2", true}, qpack_field_view{"custom-key", "custom-value", true},
        qpack_field_view{"", ""}, qpack_field_view{"user-agent", "abc"}, qpack_field_view{"Mixed-Name", "\0\xff"sv}};
    qpack_section_result owned;
    for (bool huffman : {false, true}) {
        auto encoded = encoder.encode(fields, limits, huffman);
        LT_CHECK(encoded.status.ok());
        owned = decoder.decode(qpack_octets(encoded.value), limits);
        LT_CHECK(owned.status.ok() && same(owned.fields, fields));
        LT_CHECK(encoder.encode_section(owned.fields, limits, huffman).value == encoded.value);
        encoded.value.assign(encoded.value.size(), '!');
        LT_CHECK(same(owned.fields, fields));
    }
    const auto semantic = owned.materialize();
    LT_CHECK(semantic.size() == fields.size());
    LT_CHECK(semantic.entries()[0].name == ":authority");
    LT_CHECK(semantic.all("cookie")[0] == "a=1" && semantic.all("cookie")[1] == "b=2");
    const auto relay = decoder.decode(qpack_octets(encoder.encode_fields(semantic, true, limits).value), limits);
    LT_CHECK(relay.status.ok() && relay.fields.size() == fields.size());
    for (const auto& field : relay.fields) LT_CHECK(field.never_indexed);
    // Independent bytes exercise N and literal-name H position, including length continuation.
    const auto raw = unhex("000070005f020347455425782d666f6f03626172");
    const std::array expected = {qpack_field_view{":authority", "", true}, qpack_field_view{":method", "GET"}, qpack_field_view{"x-foo", "bar"}};
    LT_CHECK(same(decoder.decode(qpack_octets(raw), limits).fields, expected));
    const auto huffman = unhex("00002f0125a849e95ba97d7f8925a849e95bb8e8b4bf");
    const std::array custom = {qpack_field_view{"custom-key", "custom-value"}};
    LT_CHECK(same(decoder.decode(qpack_octets(huffman), limits).fields, custom));
LT_END_AUTO_TEST(literals_sensitivity_order_and_owned_storage)
LT_BEGIN_AUTO_TEST(qpack_section_suite, complete_section_rejections_are_atomic_and_allocate_nothing)
    qpack_decoder decoder;
    for (auto hex : {"", "00", "0100", "0080", "0081", "000080", "000010", "00004000", "00000000",
                     "0000ff24", "00005f5400", "000051", "00005102ff", "00002f", "0000236162", "000023616263",
                     "00005184ffffffff", "0000518100", "00002bffffff00", "0000d15102ff", "03811011"}) {
        const auto wire = unhex(hex);
        allocation_observer::calls = 0;
        allocation_observer::enabled = true;
        const auto rejected = decoder.decode(qpack_octets(wire), limits);
        allocation_observer::enabled = false;
        LT_CHECK(rejected.status.state == qpack_state::malformed && rejected.fields.empty());
        LT_CHECK(allocation_observer::calls == 0);
    }
    // Truncation at every incomplete boundary, including a valid leading field.
    for (auto hex : {"0000d1510b2f696e6465782e68746d6c", "00002f0125a849e95ba97d7f8925a849e95bb8e8b4bf", "007f00ff23"}) {
        const auto wire = unhex(hex);
        for (std::size_t size = 0; size < wire.size(); ++size) {
            if (size == 2 || (hex == "0000d1510b2f696e6465782e68746d6c"sv && size == 3) || (hex == "007f00ff23"sv && size == 3)) continue;
            const auto rejected = decoder.decode(qpack_octets(wire).first(size), limits);
            LT_CHECK(rejected.status.state == qpack_state::malformed && rejected.fields.empty());
        }
    }
    LT_CHECK(decoder.decode(qpack_octets(unhex(qpack_fixture::literal_path)), limits).status.ok());
LT_END_AUTO_TEST(complete_section_rejections_are_atomic_and_allocate_nothing)
LT_BEGIN_AUTO_TEST(qpack_section_suite, exact_limits_expansion_and_exception_boundaries)
    qpack_decoder decoder;
    qpack_encoder encoder;
    const std::array fields = {qpack_field_view{":method", "GET"}, qpack_field_view{"x", "abc"}};
    const auto wire = encoder.encode(fields, limits).value;
    constexpr std::size_t expanded = 7 + 3 + 32 + 1 + 3 + 32;
    const qpack_section_limits exact{wire.size(), expanded, fields.size()};
    LT_CHECK(encoder.encode(fields, exact).status.ok());
    LT_CHECK(decoder.decode(qpack_octets(wire), exact).status.ok());
    for (auto limit : {qpack_section_limits{wire.size() - 1, expanded, 2}, qpack_section_limits{wire.size(), expanded - 1, 2},
                       qpack_section_limits{wire.size(), expanded, 1}, qpack_section_limits{wire.size(), expanded, 0}, qpack_section_limits{1, expanded, 2}}) {
        allocation_observer::enabled = true; allocation_observer::calls = 0;
        const auto rejected = decoder.decode(qpack_octets(wire), limit);
        allocation_observer::enabled = false;
        LT_CHECK(rejected.status.state == qpack_state::limit_exceeded && rejected.fields.empty());
        LT_CHECK(allocation_observer::calls == 0);
        const auto failed = encoder.encode(fields, limit);
        LT_CHECK(failed.status.state == qpack_state::limit_exceeded && failed.value.empty() && failed.consumed == 0);
    }
    const std::string compact = std::string("\0\0", 2) + std::string(20, static_cast<char>(0xc0));
    LT_CHECK(decoder.decode(qpack_octets(compact), {compact.size(), 41 * 20, 20}).status.state == qpack_state::limit_exceeded);
    const std::string large(200, 'a');
    const std::array long_field = {qpack_field_view{"x", large}};
    const auto huffman = encoder.encode(long_field, limits, true).value;
    LT_CHECK(decoder.decode(qpack_octets(huffman), {huffman.size(), 232, 1}).status.state == qpack_state::limit_exceeded);
    allocation_observer::enabled = true; allocation_observer::fail = true;
    const auto allocation_decode = decoder.decode(qpack_octets(wire), exact);
    const auto allocation_encode = encoder.encode(long_field, limits);
    allocation_observer::enabled = false; allocation_observer::fail = false;
    LT_CHECK(allocation_decode.status.state == qpack_state::limit_exceeded && allocation_decode.fields.empty());
    LT_CHECK(allocation_encode.status.state == qpack_state::limit_exceeded && allocation_encode.value.empty());
    LT_CHECK(encoder.encode(fields, exact).status.ok() && decoder.decode(qpack_octets(wire), exact).status.ok());
LT_END_AUTO_TEST(exact_limits_expansion_and_exception_boundaries)
LT_BEGIN_AUTO_TEST(qpack_section_suite, uncached_semantic_rejection_precedes_allocation)
    qpack_encoder encoder;
    const std::string large(200, 'a');
    for (auto limit : {qpack_section_limits{4096, 8192, 0}, qpack_section_limits{4096, 232, 1}, qpack_section_limits{1, 8192, 1}}) {
        httpserver::http::fields fields;
        fields.append("x", large);

        allocation_observer::enabled = true; allocation_observer::calls = 0;
        const auto rejected = encoder.encode_fields(fields, false, limit);
        allocation_observer::enabled = false;

        LT_CHECK(rejected.status.state == qpack_state::limit_exceeded && rejected.value.empty() && rejected.consumed == 0);
        LT_CHECK(allocation_observer::calls == 0);
    }
LT_END_AUTO_TEST(uncached_semantic_rejection_precedes_allocation)
LT_BEGIN_AUTO_TEST(qpack_section_suite, uncached_semantic_allocation_failure_is_atomic)
    qpack_encoder encoder;
    httpserver::http::fields fields;
    fields.append("x", std::string(200, 'a'));
    const std::length_error length_failure("injected string length failure");
    // A noexcept cache rebuild must fail the executable rather than silently pass.
    const auto terminate = std::set_terminate([] { std::_Exit(86); });

    allocation_observer::enabled = true; allocation_observer::fail = true; allocation_observer::calls = 0;
    const auto bad_alloc = encoder.encode_fields(fields, true, limits);
    allocation_observer::enabled = false; allocation_observer::fail = false;
    LT_CHECK(bad_alloc.status.state == qpack_state::limit_exceeded && bad_alloc.value.empty() && bad_alloc.consumed == 0);
    LT_CHECK(allocation_observer::calls > 0);

    allocation_observer::enabled = true; allocation_observer::length_failure = &length_failure; allocation_observer::calls = 0;
    const auto too_long = encoder.encode_fields(fields, true, limits);
    allocation_observer::enabled = false; allocation_observer::length_failure = nullptr;
    std::set_terminate(terminate);
    LT_CHECK(too_long.status.state == qpack_state::limit_exceeded && too_long.value.empty() && too_long.consumed == 0);
    LT_CHECK(allocation_observer::calls > 0);
    LT_CHECK(encoder.encode_fields(fields, true, limits).status.ok());
LT_END_AUTO_TEST(uncached_semantic_allocation_failure_is_atomic)
LT_BEGIN_AUTO_TEST(qpack_section_suite, uncached_semantic_order_sensitivity_and_exact_limits)
    qpack_encoder encoder;
    qpack_decoder decoder;
    const std::string large(200, 'a');
    httpserver::http::fields fields;
    fields.append("X", large);
    fields.append(":method", "GET");
    fields.append("x", "tail");
    constexpr std::size_t expanded = 1 + 200 + 32 + 7 + 3 + 32 + 1 + 4 + 32;
    for (bool huffman : {false, true}) {
        for (bool never_indexed : {false, true}) {
            const std::array expected = {qpack_field_view{"X", large, never_indexed}, qpack_field_view{":method", "GET", never_indexed},
                                         qpack_field_view{"X", "tail", never_indexed}};
            const auto wire = encoder.encode(expected, limits, huffman).value;
            const qpack_section_limits exact{wire.size(), expanded, 3};

            const auto encoded = encoder.encode_fields(fields, never_indexed, exact, huffman);
            const auto rejected = encoder.encode_fields(fields, never_indexed, {wire.size() - 1, expanded, 3}, huffman);

            LT_CHECK(encoded.status.ok() && encoded.value == wire);
            LT_CHECK(same(decoder.decode(qpack_octets(encoded.value), exact).fields, expected));
            LT_CHECK(rejected.status.state == qpack_state::limit_exceeded && rejected.value.empty() && rejected.consumed == 0);
        }
    }
LT_END_AUTO_TEST(uncached_semantic_order_sensitivity_and_exact_limits)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
