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

// Private HPACK primitives; field sections and connection tables belong to TASK-138.
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
inline hpack_integer_result decode_continuation(std::span<const std::uint8_t> input,
                                                std::uint64_t value, hpack_integer_limits limits) noexcept {
    unsigned shift = 0;
    for (std::size_t pos = 1; ; ++pos) {
        if (pos >= limits.max_octets) return {{hpack_state::limit_exceeded}};
        if (pos >= input.size()) return {{hpack_state::incomplete}};
        const std::uint64_t payload = input[pos] & 0x7f;
        // Division proves the shift/add fits before either operation executes.
        const std::uint64_t factor = std::uint64_t{1} << shift;  // shift <= 63
        if (payload > (UINT64_MAX - value) / factor) return {{hpack_state::malformed}};
        if (payload > (limits.max_value - value) / factor) return {{hpack_state::limit_exceeded}};
        value += payload * factor;
        if ((input[pos] & 0x80) == 0) return {{}, value, pos + 1};
        // The independent octet ceiling also bounds nonminimal zero chains.
        if (shift == 63) return {{hpack_state::limit_exceeded}};
        shift += 7;
    }
}
inline std::size_t integer_size(std::uint64_t value, std::uint64_t mask) noexcept {
    if (value < mask) return 1;
    std::size_t size = 1;
    auto remaining = value - mask;
    do {
        ++size;
        remaining /= 128;
    } while (remaining != 0);
    return size;
}
inline void write_integer(std::uint64_t value, std::uint64_t mask, std::uint8_t high_bits, std::string& output) noexcept {
    output[0] = static_cast<char>(high_bits | (value < mask ? value : mask));
    if (value < mask) return;
    auto remaining = value - mask;
    for (std::size_t pos = 1; pos < output.size(); ++pos) {
        output[pos] = static_cast<char>((remaining & 0x7f) | (pos + 1 < output.size() ? 0x80 : 0));
        remaining /= 128;
    }
}
}  // namespace hpack_codec

inline hpack_integer_result hpack_decode_integer(std::span<const std::uint8_t> input,
                                                unsigned prefix, hpack_integer_limits limits) noexcept {
    if (!hpack_valid_integer_parameters(prefix, limits)) return {{hpack_state::invalid_argument}};
    if (input.empty()) return {{hpack_state::incomplete}};
    const std::uint64_t mask = (1U << prefix) - 1;
    const std::uint64_t value = input[0] & mask;
    if (value > limits.max_value) return {{hpack_state::limit_exceeded}};
    if (value < mask) return {{}, value, 1};
    return hpack_codec::decode_continuation(input, value, limits);
}

inline hpack_bytes_result hpack_encode_integer(std::uint64_t value, unsigned prefix,
                                               std::uint8_t high_bits, hpack_integer_limits limits,
                                               std::size_t output_limit) {
    if (!hpack_valid_integer_parameters(prefix, limits)) return {{hpack_state::invalid_argument}, {}};
    const std::uint64_t mask = (1U << prefix) - 1;
    if ((high_bits & mask) != 0) return {{hpack_state::invalid_argument}, {}};
    if (value > limits.max_value) return {{hpack_state::limit_exceeded}, {}};
    const auto size = hpack_codec::integer_size(value, mask);
    if (size > limits.max_octets || size > output_limit) return {{hpack_state::limit_exceeded}, {}};
    std::string output(size, '\0');
    hpack_codec::write_integer(value, mask, high_bits, output);
    return {{}, std::move(output), size};
}

// These are payload and expanded-output allowances, independent of connection
// table capacity. The future field-section owner supplies its remaining budget.
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
inline hpack_status walk_bit(huffman_walk& walk, unsigned bit, std::size_t limit, char* output) noexcept {
    walk.node = hpack_huffman_trie.nodes[walk.node].child[bit];
    if (walk.node == -1) return {hpack_state::malformed};
    ++walk.tail_bits;
    walk.tail_ones = walk.tail_ones && bit == 1;
    const int symbol = hpack_huffman_trie.nodes[walk.node].symbol;
    if (symbol == 256) return {hpack_state::malformed};  // EOS is never data.
    if (symbol == -1) return {};
    if (walk.count == limit) return {hpack_state::limit_exceeded};
    if (output != nullptr) output[walk.count] = static_cast<char>(symbol);
    ++walk.count;
    walk.node = 0;
    walk.tail_bits = 0;
    walk.tail_ones = true;
    return {};
}
inline hpack_integer_result walk_huffman(std::span<const std::uint8_t> input,
                                         std::size_t decoded_limit, char* output) noexcept {
    huffman_walk walk;
    for (const auto octet : input) {
        for (unsigned bit = 8; bit > 0; --bit) {
            const auto status = walk_bit(walk, (octet >> (bit - 1)) & 1, decoded_limit, output);
            if (!status.ok()) return {status};
        }
    }
    // Only 0-7 bits of the all-ones EOS prefix are valid terminal padding.
    if (walk.tail_bits > 7 || !walk.tail_ones) return {{hpack_state::malformed}};
    return {{}, walk.count, input.size()};
}

inline hpack_integer_result encoded_size(std::span<const std::uint8_t> input,
                                         hpack_string_limits limits, std::size_t output_limit) noexcept {
    if (input.size() > limits.max_decoded_bytes) return {{hpack_state::limit_exceeded}};
    const auto cap = std::min(limits.max_encoded_bytes, output_limit);
    std::size_t bytes = 0;
    unsigned residual_bits = 0;
    for (const auto octet : input) {
        const auto bits = residual_bits + hpack_huffman_codes[octet].length;
        const std::size_t added = bits / 8;
        if (added > cap - bytes) return {{hpack_state::limit_exceeded}};
        bytes += added;
        residual_bits = bits % 8;
    }
    if (residual_bits != 0) {
        if (bytes == cap) return {{hpack_state::limit_exceeded}};
        ++bytes;
    }
    return {{}, bytes, 0};
}

// Writes only after exact admission. The accumulator holds one octet, so even
// the longest 30-bit codeword cannot cause an oversized shift.
inline void write_huffman(std::span<const std::uint8_t> input, char* output) noexcept {
    unsigned accumulator = 0;
    unsigned bits = 0;
    std::size_t pos = 0;
    for (const auto octet : input) {
        const auto code = hpack_huffman_codes[octet];
        for (unsigned bit = code.length; bit > 0; --bit) {
            accumulator = (accumulator << 1) | ((code.code >> (bit - 1)) & 1);
            if (++bits == 8) {
                output[pos++] = static_cast<char>(accumulator);
                accumulator = 0;
                bits = 0;
            }
        }
    }
    if (bits != 0) output[pos] = static_cast<char>((accumulator << (8 - bits)) | ((1U << (8 - bits)) - 1));
}
}  // namespace hpack_codec

inline hpack_bytes_result hpack_decode_huffman(std::span<const std::uint8_t> input, hpack_string_limits limits) {
    if (input.size() > limits.max_encoded_bytes) return {{hpack_state::limit_exceeded}, {}};
    const auto admitted = hpack_codec::walk_huffman(input, limits.max_decoded_bytes, nullptr);
    if (!admitted.status.ok()) return {admitted.status, {}};
    if (admitted.value > std::string{}.max_size()) return {{hpack_state::limit_exceeded}, {}};
    // Malformed and over-budget payloads never allocate output. Replay emits
    // exactly the approved count from the same immutable borrowed input.
    std::string output(static_cast<std::size_t>(admitted.value), '\0');
    hpack_codec::walk_huffman(input, limits.max_decoded_bytes, output.data());
    return {{}, std::move(output), input.size()};
}

inline hpack_bytes_result hpack_encode_huffman(std::span<const std::uint8_t> input,
                                               hpack_string_limits limits, std::size_t output_limit) {
    const auto admitted = hpack_codec::encoded_size(input, limits, output_limit);
    if (!admitted.status.ok()) return {admitted.status, {}};
    if (admitted.value > std::string{}.max_size()) return {{hpack_state::limit_exceeded}, {}};
    std::string output(static_cast<std::size_t>(admitted.value), '\0');
    hpack_codec::write_huffman(input, output.data());
    return {{}, std::move(output), static_cast<std::size_t>(admitted.value)};
}

inline hpack_bytes_result hpack_decode_string(std::span<const std::uint8_t> input, hpack_string_limits limits) {
    const auto length = hpack_decode_integer(input, 7, {});
    if (!length.status.ok()) return {length.status, {}};
    if (length.value > limits.max_encoded_bytes || length.value > SIZE_MAX) return {{hpack_state::limit_exceeded}, {}};
    const auto size = static_cast<std::size_t>(length.value);
    if (size > input.size() - length.consumed) return {{hpack_state::incomplete}, {}};
    const auto payload = input.subspan(length.consumed, size);
    if ((input[0] & 0x80) != 0) {
        auto result = hpack_decode_huffman(payload, limits);
        if (result.status.ok()) result.consumed = length.consumed + size;
        return result;
    }
    if (size > limits.max_decoded_bytes || size > std::string{}.max_size()) return {{hpack_state::limit_exceeded}, {}};
    std::string output(size, '\0');
    if (size != 0) std::copy(payload.begin(), payload.end(), output.begin());
    return {{}, std::move(output), length.consumed + size};
}

inline hpack_bytes_result hpack_encode_string(std::span<const std::uint8_t> input, bool huffman,
                                              hpack_string_limits limits, std::size_t output_limit) {
    if (input.size() > limits.max_decoded_bytes) return {{hpack_state::limit_exceeded}, {}};
    std::size_t payload_size = input.size();
    if (huffman) {
        const auto admitted = hpack_codec::encoded_size(input, limits, output_limit);
        if (!admitted.status.ok()) return {admitted.status, {}};
        payload_size = static_cast<std::size_t>(admitted.value);
    }
    if (payload_size > limits.max_encoded_bytes || payload_size > output_limit) return {{hpack_state::limit_exceeded}, {}};
    const auto prefix = hpack_encode_integer(payload_size, 7, huffman ? 0x80 : 0, {}, output_limit - payload_size);
    if (!prefix.status.ok()) return {prefix.status, {}};
    const auto total = prefix.value.size() + payload_size;  // proven <= output_limit
    if (total > std::string{}.max_size()) return {{hpack_state::limit_exceeded}, {}};
    std::string output(total, '\0');
    std::copy(prefix.value.begin(), prefix.value.end(), output.begin());
    if (huffman) {
        hpack_codec::write_huffman(input, output.data() + prefix.value.size());
    } else {
        std::copy(input.begin(), input.end(), output.begin() + prefix.value.size());
    }
    return {{}, std::move(output), total};
}

}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_PRIMITIVES_HPP_
