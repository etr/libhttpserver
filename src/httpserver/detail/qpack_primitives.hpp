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
enum class qpack_state { ok, incomplete, invalid_argument, malformed, limit_exceeded, inserts_unavailable, eviction_prohibited };
struct qpack_status {
    qpack_state state = qpack_state::ok;
    constexpr bool ok() const noexcept { return state == qpack_state::ok; }
    constexpr http::outcome_code code() const noexcept {
        switch (state) {
            case qpack_state::ok: return http::outcome_code::ok;
            case qpack_state::inserts_unavailable:
            case qpack_state::eviction_prohibited:
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
            case qpack_state::inserts_unavailable: return "required QPACK inserts unavailable";
            case qpack_state::eviction_prohibited: return "QPACK eviction prohibited";
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
qpack_status translate(hpack_status status) noexcept;
inline bool valid(qpack_integer_limits limits) noexcept {
    return limits.max_value <= qpack_integer_maximum && limits.max_octets >= 1 && limits.max_octets <= 10;
}
inline bool valid_string(unsigned prefix, std::uint8_t flag) noexcept {
    return prefix >= 1 && prefix <= 7 && flag == (1U << prefix);
}
}  // namespace qpack_codec
qpack_integer_result qpack_decode_integer(std::span<const std::uint8_t> input, unsigned prefix, qpack_integer_limits limits) noexcept;
qpack_bytes_result qpack_encode_integer(std::uint64_t value, unsigned prefix, std::uint8_t high_bits,
                                              qpack_integer_limits limits, std::size_t output_limit);
// Allocation-free length/Huffman admission shared by primitive and section decode.
qpack_integer_result qpack_inspect_string(std::span<const std::uint8_t> input, qpack_string_limits limits,
                                                unsigned prefix = 7, std::uint8_t flag = 0x80) noexcept;
qpack_bytes_result qpack_decode_string(std::span<const std::uint8_t> input, qpack_string_limits limits,
                                             unsigned prefix = 7, std::uint8_t flag = 0x80);
qpack_bytes_result qpack_encode_string(std::span<const std::uint8_t> input, bool huffman,
                                             qpack_string_limits limits, std::size_t output_limit,
                                             unsigned prefix = 7, std::uint8_t flag = 0x80, std::uint8_t high_bits = 0);
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_PRIMITIVES_HPP_
