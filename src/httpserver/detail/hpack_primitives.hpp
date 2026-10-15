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

// Private HPACK primitives composed by the connection-owned field-section codec.
#if !defined(HTTPSERVER_COMPILATION)
#error "hpack_primitives.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HPACK_PRIMITIVES_HPP_
#define SRC_HTTPSERVER_DETAIL_HPACK_PRIMITIVES_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <httpserver/detail/hpack_huffman_table.hpp>
#include <httpserver/http/outcome.hpp>

namespace httpserver::detail {

enum class hpack_state { ok, incomplete, invalid_argument, malformed, limit_exceeded };

// No allocated diagnostics or committed output on failure. Incomplete means the
// enclosing bounded frame/block collector may retry with additional input.
struct hpack_status {
    hpack_state state = hpack_state::ok;
    constexpr bool ok() const noexcept { return state == hpack_state::ok; }
    constexpr http::outcome_code code() const noexcept {
        switch (state) {
            case hpack_state::ok: return http::outcome_code::ok;
            case hpack_state::incomplete: return http::outcome_code::invalid_state;
            case hpack_state::invalid_argument: return http::outcome_code::invalid_argument;
            case hpack_state::malformed: return http::outcome_code::protocol_error;
            case hpack_state::limit_exceeded: return http::outcome_code::limit_exceeded;
        }
        return http::outcome_code::invalid_state;
    }
    constexpr std::string_view message() const noexcept {
        switch (state) {
            case hpack_state::ok: return {};
            case hpack_state::incomplete: return "incomplete HPACK primitive";
            case hpack_state::invalid_argument: return "invalid HPACK parameters";
            case hpack_state::malformed: return "malformed HPACK primitive";
            case hpack_state::limit_exceeded: return "HPACK primitive limit exceeded";
        }
        return "invalid HPACK status";
    }
};

struct hpack_integer_limits {
    std::uint64_t max_value = UINT64_MAX;
    // A uint64_t needs at most ten continuation bytes plus the prefix byte.
    // Smaller ceilings are supported; larger ones are invalid parameters.
    std::size_t max_octets = 11;
};
struct hpack_integer_result {
    hpack_status status;
    std::uint64_t value = 0;
    std::size_t consumed = 0;
};
struct hpack_bytes_result {
    hpack_status status;
    std::string value;
    // Decoders consume input; encoders report the produced wire size.
    std::size_t consumed = 0;
};

inline bool hpack_valid_integer_parameters(unsigned prefix, hpack_integer_limits limits) noexcept {
    return prefix >= 1 && prefix <= 8 && limits.max_octets >= 1 && limits.max_octets <= 11;
}

namespace hpack_codec {
hpack_integer_result decode_continuation(std::span<const std::uint8_t> input,
                                                std::uint64_t value, hpack_integer_limits limits) noexcept;
std::size_t integer_size(std::uint64_t value, std::uint64_t mask) noexcept;
void write_integer(std::uint64_t value, std::uint64_t mask, std::uint8_t high_bits, std::string& output) noexcept;
}  // namespace hpack_codec

hpack_integer_result hpack_decode_integer(std::span<const std::uint8_t> input,
                                                unsigned prefix, hpack_integer_limits limits) noexcept;

hpack_bytes_result hpack_encode_integer(std::uint64_t value, unsigned prefix,
                                               std::uint8_t high_bits, hpack_integer_limits limits,
                                               std::size_t output_limit);

// These are payload and expanded-output allowances, independent of connection
// table capacity. The field-section owner supplies its remaining budget.
struct hpack_string_limits {
    std::size_t max_encoded_bytes;
    std::size_t max_decoded_bytes;
};

namespace hpack_codec {
struct huffman_walk {
    int node = 0;
    unsigned tail_bits = 0;
    bool tail_ones = true;
    std::size_t count = 0;
};
hpack_status walk_bit(huffman_walk& walk, unsigned bit, std::size_t limit, char* output) noexcept;
hpack_integer_result walk_huffman(std::span<const std::uint8_t> input,
                                         std::size_t decoded_limit, char* output) noexcept;

hpack_integer_result encoded_size(std::span<const std::uint8_t> input,
                                         hpack_string_limits limits, std::size_t output_limit) noexcept;

// Writes only after exact admission. The accumulator holds one octet, so even
// the longest 30-bit codeword cannot cause an oversized shift.
void write_huffman(std::span<const std::uint8_t> input, char* output) noexcept;
}  // namespace hpack_codec

hpack_bytes_result hpack_decode_huffman(std::span<const std::uint8_t> input, hpack_string_limits limits);

hpack_bytes_result hpack_encode_huffman(std::span<const std::uint8_t> input,
                                               hpack_string_limits limits, std::size_t output_limit);

hpack_bytes_result hpack_decode_string(std::span<const std::uint8_t> input, hpack_string_limits limits);

hpack_bytes_result hpack_encode_string(std::span<const std::uint8_t> input, bool huffman,
                                              hpack_string_limits limits, std::size_t output_limit);

}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_PRIMITIVES_HPP_
