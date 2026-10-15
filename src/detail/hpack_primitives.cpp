/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>

#include <httpserver/detail/hpack_primitives.hpp>

namespace httpserver::detail::hpack_codec {

hpack_integer_result decode_continuation(std::span<const std::uint8_t> input, std::uint64_t value, hpack_integer_limits limits) noexcept {
    unsigned shift = 0;
    for (std::size_t pos = 1;; ++pos) {
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

std::size_t integer_size(std::uint64_t value, std::uint64_t mask) noexcept {
    if (value < mask) return 1;
    std::size_t size = 1;
    auto remaining = value - mask;
    do {
        ++size;
        remaining /= 128;
    } while (remaining != 0);
    return size;
}

void write_integer(std::uint64_t value, std::uint64_t mask, std::uint8_t high_bits, std::string& output) noexcept {
    output[0] = static_cast<char>(high_bits | (value < mask ? value : mask));
    if (value < mask) return;
    auto remaining = value - mask;
    for (std::size_t pos = 1; pos < output.size(); ++pos) {
        output[pos] = static_cast<char>((remaining & 0x7f) | (pos + 1 < output.size() ? 0x80 : 0));
        remaining /= 128;
    }
}

}  // namespace httpserver::detail::hpack_codec

namespace httpserver::detail {

hpack_integer_result hpack_decode_integer(std::span<const std::uint8_t> input, unsigned prefix, hpack_integer_limits limits) noexcept {
    if (!hpack_valid_integer_parameters(prefix, limits)) return {{hpack_state::invalid_argument}};
    if (input.empty()) return {{hpack_state::incomplete}};
    const std::uint64_t mask = (1U << prefix) - 1;
    const std::uint64_t value = input[0] & mask;
    if (value > limits.max_value) return {{hpack_state::limit_exceeded}};
    if (value < mask) return {{}, value, 1};
    return hpack_codec::decode_continuation(input, value, limits);
}

hpack_bytes_result hpack_encode_integer(std::uint64_t value, unsigned prefix, std::uint8_t high_bits, hpack_integer_limits limits, std::size_t output_limit) {
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

}  // namespace httpserver::detail

namespace httpserver::detail::hpack_codec {

hpack_status walk_bit(huffman_walk& walk, unsigned bit, std::size_t limit, char* output) noexcept {
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

hpack_integer_result walk_huffman(std::span<const std::uint8_t> input, std::size_t decoded_limit, char* output) noexcept {
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

hpack_integer_result encoded_size(std::span<const std::uint8_t> input, hpack_string_limits limits, std::size_t output_limit) noexcept {
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

void write_huffman(std::span<const std::uint8_t> input, char* output) noexcept {
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

}  // namespace httpserver::detail::hpack_codec

namespace httpserver::detail {

hpack_bytes_result hpack_decode_huffman(std::span<const std::uint8_t> input, hpack_string_limits limits) {
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

hpack_bytes_result hpack_encode_huffman(std::span<const std::uint8_t> input, hpack_string_limits limits, std::size_t output_limit) {
    const auto admitted = hpack_codec::encoded_size(input, limits, output_limit);
    if (!admitted.status.ok()) return {admitted.status, {}};
    if (admitted.value > std::string{}.max_size()) return {{hpack_state::limit_exceeded}, {}};
    std::string output(static_cast<std::size_t>(admitted.value), '\0');
    hpack_codec::write_huffman(input, output.data());
    return {{}, std::move(output), static_cast<std::size_t>(admitted.value)};
}

hpack_bytes_result hpack_decode_string(std::span<const std::uint8_t> input, hpack_string_limits limits) {
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

hpack_bytes_result hpack_encode_string(std::span<const std::uint8_t> input, bool huffman, hpack_string_limits limits, std::size_t output_limit) {
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
