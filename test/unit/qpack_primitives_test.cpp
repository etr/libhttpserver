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
#include <vector>
#include <httpserver/detail/qpack_primitives.hpp>
#include "./littletest.hpp"
using httpserver::detail::qpack_bytes_result;
using httpserver::detail::qpack_decode_integer;
using httpserver::detail::qpack_decode_string;
using httpserver::detail::qpack_encode_integer;
using httpserver::detail::qpack_encode_string;
using httpserver::detail::qpack_inspect_string;
using httpserver::detail::qpack_state;
using httpserver::detail::qpack_status;
using std::string_view_literals::operator""sv;
namespace {
std::span<const std::uint8_t> bytes(std::string_view s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}
bool failed(const qpack_bytes_result& r, qpack_state state) {
    return r.status.state == state && r.value.empty() && r.consumed == 0;
}
}  // namespace
LT_BEGIN_SUITE(qpack_primitives_suite)
    void set_up() {}
    void tear_down() {}
LT_END_SUITE(qpack_primitives_suite)
LT_BEGIN_AUTO_TEST(qpack_primitives_suite, integer_boundaries)
    constexpr auto maximum = (UINT64_C(1) << 62) - 1;
    for (unsigned prefix : {3U, 4U, 6U, 7U, 8U}) {
        const std::uint64_t mask = (1U << prefix) - 1;
        for (auto value : {UINT64_C(0), mask - 1, mask, mask + 1, mask + 127, mask + 128, maximum}) {
            const auto encoded = qpack_encode_integer(value, prefix, 0, {}, 10);
            LT_CHECK(encoded.status.ok() && encoded.consumed == encoded.value.size());
            const auto decoded = qpack_decode_integer(bytes(encoded.value), prefix, {});
            LT_CHECK(decoded.status.ok() && decoded.value == value && decoded.consumed == encoded.value.size());
            LT_CHECK(failed(qpack_encode_integer(value, prefix, 0, {}, encoded.value.size() - 1), qpack_state::limit_exceeded));
            for (std::size_t size = 0; size < encoded.value.size(); ++size) {
                const auto cut = qpack_decode_integer(bytes(encoded.value).first(size), prefix, {});
                LT_CHECK(cut.status.state == qpack_state::incomplete && cut.consumed == 0 && cut.value == 0);
            }
        }
    }
    const auto too_large = qpack_decode_integer(bytes("\xff\x80\x80\x80\x80\x80\x80\x80\x80\x40"sv), 8, {});
    LT_CHECK(too_large.status.state == qpack_state::limit_exceeded && too_large.consumed == 0);
    std::string chain(12, static_cast<char>(0x80)); chain[0] = 0xff;
    LT_CHECK(qpack_decode_integer(bytes(chain), 8, {}).status.state == qpack_state::limit_exceeded);
    LT_CHECK(qpack_decode_integer(bytes("\x0a"sv), 4, {9, 10}).status.state == qpack_state::limit_exceeded);
    LT_CHECK(qpack_decode_integer(bytes("\x0f\x00"sv), 4, {maximum, 1}).status.state == qpack_state::limit_exceeded);
    LT_CHECK(qpack_decode_integer({}, 0, {}).status.state == qpack_state::invalid_argument);
    LT_CHECK(qpack_decode_integer({}, 9, {}).status.state == qpack_state::invalid_argument);
    LT_CHECK(qpack_decode_integer({}, 4, {maximum, 0}).status.state == qpack_state::invalid_argument);
    LT_CHECK(qpack_decode_integer({}, 4, {maximum, 11}).status.state == qpack_state::invalid_argument);
    LT_CHECK(failed(qpack_encode_integer(maximum + 1, 8, 0, {}, 100), qpack_state::limit_exceeded));
    LT_CHECK(failed(qpack_encode_integer(0, 4, 1, {}, 100), qpack_state::invalid_argument));
    LT_CHECK(qpack_status{qpack_state::malformed}.code() == httpserver::http::outcome_code::protocol_error);
    LT_CHECK(qpack_status{qpack_state::incomplete}.message() == "incomplete QPACK primitive");
LT_END_AUTO_TEST(integer_boundaries)
LT_BEGIN_AUTO_TEST(qpack_primitives_suite, literal_names_values_and_huffman)
    const std::string every_octet = [] { std::string s; for (unsigned i = 0; i < 256; ++i) s += static_cast<char>(i); return s; }();
    for (auto text : {""sv, "www.example.com"sv, "custom-key"sv, std::string_view(every_octet)}) {
        for (unsigned prefix : {3U, 7U}) {
            const auto flag = static_cast<std::uint8_t>(1U << prefix);
            for (bool huffman : {false, true}) {
                const auto encoded = qpack_encode_string(bytes(text), huffman, {1024, text.size()}, 1024, prefix, flag, 0);
                LT_CHECK(encoded.status.ok());
                const auto inspect = qpack_inspect_string(bytes(encoded.value), {1024, text.size()}, prefix, flag);
                LT_CHECK(inspect.status.ok() && inspect.value == text.size() && inspect.consumed == encoded.value.size());
                const auto decoded = qpack_decode_string(bytes(encoded.value), {1024, text.size()}, prefix, flag);
                LT_CHECK(decoded.status.ok() && decoded.value == text && decoded.consumed == encoded.value.size());
                LT_CHECK(failed(qpack_encode_string(bytes(text), huffman, {1024, text.size()}, encoded.value.size() - 1, prefix, flag, 0), qpack_state::limit_exceeded));
                for (std::size_t size = 0; size < encoded.value.size(); ++size) {
                    LT_CHECK(failed(qpack_decode_string(bytes(encoded.value).first(size), {1024, text.size()}, prefix, flag), qpack_state::incomplete));
                }
                if (!text.empty()) {
                    LT_CHECK(failed(qpack_decode_string(bytes(encoded.value), {1024, text.size() - 1}, prefix, flag), qpack_state::limit_exceeded));
                    LT_CHECK(failed(qpack_encode_string(bytes(text), huffman, {1024, text.size() - 1}, 1024, prefix, flag, 0), qpack_state::limit_exceeded));
                    const auto payload = qpack_decode_integer(bytes(encoded.value), prefix, {}).value;
                    LT_CHECK(failed(qpack_decode_string(bytes(encoded.value), {static_cast<std::size_t>(payload - 1), 1024}, prefix, flag), qpack_state::limit_exceeded));
                    LT_CHECK(failed(qpack_encode_string(bytes(text), huffman, {static_cast<std::size_t>(payload - 1), 1024}, 1024, prefix, flag, 0), qpack_state::limit_exceeded));
                }
            }
        }
    }
    // HPACK RFC 7541 C.4: same Huffman alphabet, independent expected payload.
    LT_CHECK(qpack_encode_string(bytes("www.example.com"), true, {100, 100}, 100).value == "\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff"sv);
    LT_CHECK(failed(qpack_decode_string(bytes("\x84\xff\xff\xff\xff"sv), {100, 100}), qpack_state::malformed));
    LT_CHECK(failed(qpack_decode_string(bytes("\x81\x00"sv), {100, 100}), qpack_state::malformed));
    LT_CHECK(failed(qpack_decode_string(bytes("\x81\xff"sv), {100, 100}), qpack_state::malformed));
    std::string chain(12, static_cast<char>(0x80)); chain[0] = 0xff;
    LT_CHECK(failed(qpack_decode_string(bytes(chain), {100, 100}), qpack_state::limit_exceeded));
    LT_CHECK(failed(qpack_decode_string({}, {100, 100}, 3, 0x80), qpack_state::invalid_argument));
    LT_CHECK(failed(qpack_encode_string({}, true, {100, 100}, 100, 3, 8, 8), qpack_state::invalid_argument));
LT_END_AUTO_TEST(literal_names_values_and_huffman)
LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
