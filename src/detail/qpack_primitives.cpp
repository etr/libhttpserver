/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>

#include <httpserver/detail/qpack_primitives.hpp>

namespace httpserver::detail::qpack_codec {

qpack_status translate(hpack_status status) noexcept {
    switch (status.state) {
        case hpack_state::ok:
            return {};
        case hpack_state::incomplete:
            return {qpack_state::incomplete};
        case hpack_state::invalid_argument:
            return {qpack_state::invalid_argument};
        case hpack_state::malformed:
            return {qpack_state::malformed};
        case hpack_state::limit_exceeded:
            return {qpack_state::limit_exceeded};
    }
    return {qpack_state::invalid_argument};
}

}  // namespace httpserver::detail::qpack_codec

namespace httpserver::detail {

qpack_integer_result qpack_decode_integer(std::span<const std::uint8_t> input, unsigned prefix, qpack_integer_limits limits) noexcept {
    if (!qpack_codec::valid(limits)) return {{qpack_state::invalid_argument}};
    const auto result = hpack_decode_integer(input, prefix, {limits.max_value, limits.max_octets});
    return {qpack_codec::translate(result.status), result.value, result.consumed};
}

qpack_bytes_result qpack_encode_integer(std::uint64_t value, unsigned prefix, std::uint8_t high_bits, qpack_integer_limits limits, std::size_t output_limit) {
    if (!qpack_codec::valid(limits)) return {{qpack_state::invalid_argument}, {}};
    auto result = hpack_encode_integer(value, prefix, high_bits, {limits.max_value, limits.max_octets}, output_limit);
    return {qpack_codec::translate(result.status), std::move(result.value), result.consumed};
}

qpack_integer_result qpack_inspect_string(std::span<const std::uint8_t> input, qpack_string_limits limits, unsigned prefix, std::uint8_t flag) noexcept {
    if (!qpack_codec::valid_string(prefix, flag)) return {{qpack_state::invalid_argument}};
    const auto length = qpack_decode_integer(input, prefix, {});
    if (!length.status.ok()) return length;
    if (length.value > limits.max_encoded_bytes || length.value > SIZE_MAX) return {{qpack_state::limit_exceeded}};
    const auto size = static_cast<std::size_t>(length.value);
    if (size > input.size() - length.consumed) return {{qpack_state::incomplete}};
    if ((input[0] & flag) == 0) {
        if (size > limits.max_decoded_bytes) return {{qpack_state::limit_exceeded}};
        return {{}, size, length.consumed + size};
    }
    const auto result = hpack_codec::walk_huffman(input.subspan(length.consumed, size), limits.max_decoded_bytes, nullptr);
    if (!result.status.ok()) return {qpack_codec::translate(result.status)};
    return {{}, result.value, length.consumed + size};
}

qpack_bytes_result qpack_decode_string(std::span<const std::uint8_t> input, qpack_string_limits limits, unsigned prefix, std::uint8_t flag) {
    const auto admitted = qpack_inspect_string(input, limits, prefix, flag);
    if (!admitted.status.ok()) return {admitted.status, {}};
    if (admitted.value > std::string{}.max_size()) return {{qpack_state::limit_exceeded}, {}};
    const auto length = qpack_decode_integer(input, prefix, {});
    const auto payload = input.subspan(length.consumed, static_cast<std::size_t>(length.value));
    std::string output(static_cast<std::size_t>(admitted.value), '\0');
    if ((input[0] & flag) != 0) {
        hpack_codec::walk_huffman(payload, limits.max_decoded_bytes, output.data());
    } else {
        std::copy(payload.begin(), payload.end(), output.begin());
    }
    return {{}, std::move(output), admitted.consumed};
}

qpack_bytes_result qpack_encode_string(std::span<const std::uint8_t> input, bool huffman, qpack_string_limits limits, std::size_t output_limit, unsigned prefix, std::uint8_t flag,
                                       std::uint8_t high_bits) {
    if (!qpack_codec::valid_string(prefix, flag) || (high_bits & ((flag << 1) - 1)) != 0) return {{qpack_state::invalid_argument}, {}};
    if (input.size() > limits.max_decoded_bytes) return {{qpack_state::limit_exceeded}, {}};
    std::size_t payload_size = input.size();
    if (huffman) {
        const auto admitted = hpack_codec::encoded_size(input, {limits.max_encoded_bytes, limits.max_decoded_bytes}, output_limit);
        if (!admitted.status.ok()) return {qpack_codec::translate(admitted.status), {}};
        payload_size = static_cast<std::size_t>(admitted.value);
    }
    if (payload_size > limits.max_encoded_bytes || payload_size > output_limit) return {{qpack_state::limit_exceeded}, {}};
    const auto length = qpack_encode_integer(payload_size, prefix, high_bits | (huffman ? flag : 0), {}, output_limit - payload_size);
    if (!length.status.ok()) return length;
    const auto total = length.value.size() + payload_size;
    if (total > std::string{}.max_size()) return {{qpack_state::limit_exceeded}, {}};
    std::string output(total, '\0');
    std::copy(length.value.begin(), length.value.end(), output.begin());
    if (huffman) {
        hpack_codec::write_huffman(input, output.data() + length.value.size());
    } else {
        std::copy(input.begin(), input.end(), output.begin() + length.value.size());
    }
    return {{}, std::move(output), total};
}

}  // namespace httpserver::detail
