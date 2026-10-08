// Appendix data from RFC 7541, Copyright (c) 2015 IETF Trust and the
// persons identified as authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#if !defined(HTTPSERVER_COMPILATION)
#error "qpack_primitives.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QPACK_PRIMITIVES_HPP_
#define SRC_HTTPSERVER_DETAIL_QPACK_PRIMITIVES_HPP_
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <httpserver/detail/hpack_primitives.hpp>
namespace httpserver::detail {
enum class qpack_state { ok, incomplete, invalid_argument, malformed, limit_exceeded };
struct qpack_status {
    qpack_state state = qpack_state::ok;
    constexpr bool ok() const noexcept { return state == qpack_state::ok; }
    constexpr http::outcome_code code() const noexcept {
        switch (state) {
            case qpack_state::ok: return http::outcome_code::ok;
            case qpack_state::incomplete: return http::outcome_code::invalid_state;
            case qpack_state::invalid_argument: return http::outcome_code::invalid_argument;
            case qpack_state::malformed: return http::outcome_code::protocol_error;
            case qpack_state::limit_exceeded: return http::outcome_code::limit_exceeded;
        }
        return http::outcome_code::invalid_state;
    }
    constexpr std::string_view message() const noexcept {
        switch (state) {
            case qpack_state::ok: return {};
            case qpack_state::incomplete: return "incomplete QPACK primitive";
            case qpack_state::invalid_argument: return "invalid QPACK parameters";
            case qpack_state::malformed: return "malformed QPACK data";
            case qpack_state::limit_exceeded: return "QPACK limit exceeded";
        }
        return "invalid QPACK status";
    }
};
inline constexpr std::uint64_t qpack_integer_maximum = (UINT64_C(1) << 62) - 1;
struct qpack_integer_limits {
    std::uint64_t max_value = qpack_integer_maximum;
    // Ten total octets suffice for every supported 62-bit value.
    std::size_t max_octets = 10;
};
struct qpack_integer_result {
    qpack_status status;
    std::uint64_t value = 0;
    std::size_t consumed = 0;
};
struct qpack_bytes_result {
    qpack_status status;
    std::string value;
    std::size_t consumed = 0;
};
struct qpack_string_limits {
    std::size_t max_encoded_bytes;
    std::size_t max_decoded_bytes;
};
namespace qpack_codec {
inline qpack_status translate(hpack_status status) noexcept {
    switch (status.state) {
        case hpack_state::ok: return {};
        case hpack_state::incomplete: return {qpack_state::incomplete};
        case hpack_state::invalid_argument: return {qpack_state::invalid_argument};
        case hpack_state::malformed: return {qpack_state::malformed};
        case hpack_state::limit_exceeded: return {qpack_state::limit_exceeded};
    }
    return {qpack_state::invalid_argument};
}
inline bool valid(qpack_integer_limits limits) noexcept {
    return limits.max_value <= qpack_integer_maximum && limits.max_octets >= 1 && limits.max_octets <= 10;
}
inline bool valid_string(unsigned prefix, std::uint8_t flag) noexcept {
    return prefix >= 1 && prefix <= 7 && flag == (1U << prefix);
}
}  // namespace qpack_codec
inline qpack_integer_result qpack_decode_integer(std::span<const std::uint8_t> input, unsigned prefix, qpack_integer_limits limits) noexcept {
    if (!qpack_codec::valid(limits)) return {{qpack_state::invalid_argument}};
    const auto result = hpack_decode_integer(input, prefix, {limits.max_value, limits.max_octets});
    return {qpack_codec::translate(result.status), result.value, result.consumed};
}
inline qpack_bytes_result qpack_encode_integer(std::uint64_t value, unsigned prefix, std::uint8_t high_bits,
                                              qpack_integer_limits limits, std::size_t output_limit) {
    if (!qpack_codec::valid(limits)) return {{qpack_state::invalid_argument}, {}};
    auto result = hpack_encode_integer(value, prefix, high_bits, {limits.max_value, limits.max_octets}, output_limit);
    return {qpack_codec::translate(result.status), std::move(result.value), result.consumed};
}
// Allocation-free length/Huffman admission shared by primitive and section decode.
inline qpack_integer_result qpack_inspect_string(std::span<const std::uint8_t> input, qpack_string_limits limits,
                                                unsigned prefix = 7, std::uint8_t flag = 0x80) noexcept {
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
inline qpack_bytes_result qpack_decode_string(std::span<const std::uint8_t> input, qpack_string_limits limits,
                                             unsigned prefix = 7, std::uint8_t flag = 0x80) {
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
inline qpack_bytes_result qpack_encode_string(std::span<const std::uint8_t> input, bool huffman,
                                             qpack_string_limits limits, std::size_t output_limit,
                                             unsigned prefix = 7, std::uint8_t flag = 0x80, std::uint8_t high_bits = 0) {
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
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_PRIMITIVES_HPP_
