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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

#include <httpserver/detail/hpack_primitives.hpp>
#include "fuzz/hpack_fuzz.hpp"

namespace {
using httpserver::detail::hpack_bytes_result;
using httpserver::detail::hpack_decode_huffman;
using httpserver::detail::hpack_decode_integer;
using httpserver::detail::hpack_decode_string;
using httpserver::detail::hpack_encode_huffman;
using httpserver::detail::hpack_encode_integer;
using httpserver::detail::hpack_encode_string;
using httpserver::detail::hpack_integer_limits;
using httpserver::detail::hpack_string_limits;
void require(bool condition) { if (!condition) std::abort(); }
std::span<const std::uint8_t> octets(const std::string& value) {
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}
void check_bytes(const hpack_bytes_result& first, const hpack_bytes_result& second,
                 std::size_t input_size, std::size_t output_limit) {
    require(first.status.state == second.status.state && first.value == second.value && first.consumed == second.consumed);
    if (first.status.ok()) {
        require(first.consumed <= input_size && first.value.size() <= output_limit);
    } else {
        require(first.value.empty() && first.consumed == 0);
    }
}
}  // namespace

// No network or unbounded setup: only the first 512 bytes participate; all
// output budgets are <= 64. Controls also exercise invalid parameters.
void hpack_fuzz_input(std::span<const std::uint8_t> input) {
    input = input.first(std::min(input.size(), std::size_t{512}));
    const auto control = [input](std::size_t pos) -> unsigned { return pos < input.size() ? input[pos] : 0; };
    const unsigned prefix = control(0) % 11;
    const hpack_integer_limits integers = {control(1) * 257U + control(2), control(3) % 13};
    const hpack_string_limits strings = {control(4) % 65, control(5) % 65};
    const auto output_limit = control(6) % 65;
    const auto wire = input.subspan(std::min(input.size(), std::size_t{8}));
    const std::string saved(wire.begin(), wire.end());
    const auto a = hpack_decode_integer(wire, prefix, integers);
    const auto b = hpack_decode_integer(wire, prefix, integers);
    require(a.status.state == b.status.state && a.value == b.value && a.consumed == b.consumed);
    if (a.status.ok()) {
        require(a.consumed <= wire.size() && a.consumed <= integers.max_octets && a.value <= integers.max_value);
        const auto encoded = hpack_encode_integer(a.value, prefix, 0, integers, 11);
        require(encoded.status.ok());
        const auto decoded = hpack_decode_integer(octets(encoded.value), prefix, integers);
        require(decoded.status.ok() && decoded.value == a.value);
    } else {
        require(a.value == 0 && a.consumed == 0);
    }
    const auto value = control(1) * 257U + control(2);
    const auto encoded_integer = hpack_encode_integer(value, prefix, static_cast<std::uint8_t>(control(7)), integers, output_limit);
    require(!encoded_integer.status.ok() || encoded_integer.value.size() <= output_limit);
    check_bytes(hpack_decode_string(wire, strings), hpack_decode_string(wire, strings), wire.size(), strings.max_decoded_bytes);
    check_bytes(hpack_decode_huffman(wire, strings), hpack_decode_huffman(wire, strings), wire.size(), strings.max_decoded_bytes);
    for (bool huffman : {false, true}) {
        const auto encoded = hpack_encode_string(wire, huffman, strings, output_limit);
        const auto again = hpack_encode_string(wire, huffman, strings, output_limit);
        check_bytes(encoded, again, output_limit, output_limit);
        if (encoded.status.ok()) {
            const auto decoded = hpack_decode_string(octets(encoded.value), strings);
            require(decoded.status.ok() && decoded.value == saved && decoded.consumed == encoded.value.size());
            const auto with_tail = encoded.value + "tail";
            const auto trailing = hpack_decode_string(octets(with_tail), strings);
            require(trailing.value == saved && trailing.consumed == encoded.value.size());
        }
    }
    const auto encoded = hpack_encode_huffman(wire, strings, output_limit);
    check_bytes(encoded, hpack_encode_huffman(wire, strings, output_limit), output_limit, output_limit);
    if (encoded.status.ok()) {
        const auto decoded = hpack_decode_huffman(octets(encoded.value), strings);
        require(decoded.status.ok() && decoded.value == saved);
    }
    require(std::equal(wire.begin(), wire.end(), saved.begin(), saved.end(),
                       [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); }));
}

#ifdef HPACK_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    hpack_fuzz_input({data, size});
    return 0;
}
#endif
