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
#error "qpack_encoder.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QPACK_ENCODER_HPP_
#define SRC_HTTPSERVER_DETAIL_QPACK_ENCODER_HPP_
#include <cstddef>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <httpserver/detail/qpack_field_section.hpp>
namespace httpserver::detail {
class qpack_encoder {
 public:
    qpack_bytes_result encode(std::span<const qpack_field_view> fields, qpack_section_limits limits, bool huffman = false) const {
        return encode_sequence(fields.size(), [fields](auto i) { return fields[i]; }, limits, huffman);
    }
    qpack_bytes_result encode_section(std::span<const qpack_field> fields, qpack_section_limits limits, bool huffman = false) const {
        return encode_sequence(fields.size(), [fields](auto i) { return fields[i].view(); }, limits, huffman);
    }
    // Semantic fields do not retain sensitivity; policy must be explicit.
    qpack_bytes_result encode_fields(const http::fields& fields, bool never_indexed, qpack_section_limits limits, bool huffman = false) const {
        return encode_sequence(fields.size(), [&fields, never_indexed](auto i) {
            const auto [key, value] = fields.order_[i];
            return qpack_field_view{fields.name_store_[key], fields.value_store_[key][value], never_indexed};
        }, limits, huffman);
    }

 private:
    template<typename View>
    static qpack_bytes_result encode_sequence(std::size_t count, View view, qpack_section_limits limits, bool huffman) {
        if (count > limits.max_fields || limits.max_compressed_bytes < 2) return {{qpack_state::limit_exceeded}, {}};
        auto remaining = limits.max_expanded_bytes;
        for (std::size_t i = 0; i < count; ++i) {
            const auto field = view(i);
            if (!qpack_admit_field(field.name.size(), field.value.size(), remaining)) return {{qpack_state::limit_exceeded}, {}};
        }
        try {
            // Required Insert Count=0, sign=0, Delta Base=0. Dynamic capacity is fixed at zero.
            std::string wire(2, '\0');
            for (std::size_t i = 0; i < count; ++i) {
                const auto status = write_field(view(i), huffman, wire, limits.max_compressed_bytes);
                if (!status.ok()) return {status, {}};
            }
            const auto size = wire.size();
            return {{}, std::move(wire), size};
        } catch (const std::bad_alloc&) {
            return {{qpack_state::limit_exceeded}, {}};
        } catch (const std::length_error&) {
            return {{qpack_state::limit_exceeded}, {}};
        }
    }
    static qpack_status append(qpack_bytes_result bytes, std::string& wire) {
        if (!bytes.status.ok()) return bytes.status;
        wire += bytes.value;
        return {};
    }
    static qpack_status write_string(std::string_view value, bool huffman, std::string& wire, std::size_t limit,
                                     unsigned prefix = 7, std::uint8_t flag = 0x80, std::uint8_t bits = 0) {
        const auto remaining = limit - wire.size();
        return append(qpack_encode_string(qpack_octets(value), huffman, {remaining, value.size()}, remaining, prefix, flag, bits), wire);
    }
    static qpack_status write_field(qpack_field_view field, bool huffman, std::string& wire, std::size_t limit) {
        const auto exact = qpack_static_find(field.name, field.value);
        if (exact && !field.never_indexed) return append(qpack_encode_integer(*exact, 6, 0xc0, {}, limit - wire.size()), wire);
        const auto name = qpack_static_find_name(field.name);
        qpack_status status;
        if (name) {
            status = append(qpack_encode_integer(*name, 4, field.never_indexed ? 0x70 : 0x50, {}, limit - wire.size()), wire);
        } else {
            status = write_string(field.name, huffman, wire, limit, 3, 8, field.never_indexed ? 0x30 : 0x20);
        }
        if (!status.ok()) return status;
        return write_string(field.value, huffman, wire, limit);
    }
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_ENCODER_HPP_
