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
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <httpserver/detail/qpack_field_section.hpp>
#include <httpserver/detail/qpack_dynamic_table.hpp>
namespace httpserver::detail {
struct qpack_dynamic_bytes_result {
    qpack_status status;
    std::string value;
    std::size_t consumed = 0;
    qpack_reference_hold references;
};
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

    qpack_dynamic_bytes_result encode_dynamic(std::span<const qpack_field_view> fields, qpack_dynamic_table& table,
                                              std::uint64_t base, qpack_section_limits limits, bool huffman = false) const {
        if (!table.initialized() || table.role() != qpack_table_role::encoder || base > qpack_integer_maximum) {
            return {{qpack_state::invalid_argument}, {}, 0, {}};
        }
        if (fields.size() > limits.max_fields || limits.max_compressed_bytes < 2) return {{qpack_state::limit_exceeded}, {}, 0, {}};
        auto remaining = limits.max_expanded_bytes;
        for (const auto& field : fields) {
            if (!qpack_admit_field(field.name.size(), field.value.size(), remaining)) return {{qpack_state::limit_exceeded}, {}, 0, {}};
        }
        try {
            std::vector<std::uint64_t> indexes;
            std::uint64_t required = 0;
            for (const auto& field : fields) {
                const auto dynamic = dynamic_reference(field, table);
                if (dynamic) {
                    indexes.push_back(*dynamic);
                    required = std::max(required, *dynamic + 1);
                }
            }
            const auto max_entries = table.maximum_capacity() / 32;
            if (required && max_entries == 0) return {{qpack_state::invalid_argument}, {}, 0, {}};
            const auto encoded_required = required ? (required % (2 * max_entries)) + 1 : 0;
            auto prefix_bytes = qpack_encode_integer(encoded_required, 8, 0, {}, limits.max_compressed_bytes);
            if (!prefix_bytes.status.ok()) return {prefix_bytes.status, {}, 0, {}};
            auto wire = std::move(prefix_bytes.value);
            auto status = append(qpack_encode_integer(base >= required ? base - required : required - base - 1,
                                                       7, base >= required ? 0 : 0x80, {}, limits.max_compressed_bytes - wire.size()), wire);
            if (!status.ok()) return {status, {}, 0, {}};
            for (const auto& field : fields) {
                status = write_dynamic_field(field, table, base, huffman, wire, limits.max_compressed_bytes);
                if (!status.ok()) return {status, {}, 0, {}};
            }
            qpack_dynamic_bytes_result result;
            status = table.acquire(indexes, limits.max_fields, result.references);
            if (!status.ok()) return {status, {}, 0, {}};
            result.value = std::move(wire);
            result.consumed = result.value.size();
            return result;
        } catch (const std::bad_alloc&) {
            return {{qpack_state::limit_exceeded}, {}, 0, {}};
        } catch (const std::length_error&) {
            return {{qpack_state::limit_exceeded}, {}, 0, {}};
        }
    }

 private:
    static std::optional<std::uint64_t> dynamic_reference(qpack_field_view field, const qpack_dynamic_table& table) noexcept {
        if (!field.never_indexed && qpack_static_find(field.name, field.value)) return std::nullopt;
        if (!field.never_indexed) {
            if (auto exact = table.find(field.name, field.value)) return exact;
        }
        return table.find_name(field.name);
    }
    static qpack_status write_dynamic_field(qpack_field_view field, const qpack_dynamic_table& table, std::uint64_t base,
                                             bool huffman, std::string& wire, std::size_t limit) {
        const auto dynamic = dynamic_reference(field, table);
        if (!dynamic) return write_field(field, huffman, wire, limit);
        const auto* entry = table.lookup(*dynamic);
        const bool indexed = !field.never_indexed && entry->value == field.value;
        const bool relative = *dynamic < base;
        const auto index = relative ? base - *dynamic - 1 : *dynamic - base;
        const unsigned prefix = indexed ? (relative ? 6 : 4) : (relative ? 4 : 3);
        const std::uint8_t bits = indexed ? (relative ? 0x80 : 0x10) :
            (relative ? (field.never_indexed ? 0x60 : 0x40) : (field.never_indexed ? 8 : 0));
        const auto status = append(qpack_encode_integer(index, prefix, bits, {}, limit - wire.size()), wire);
        if (!status.ok() || indexed) return status;
        return write_string(field.value, huffman, wire, limit);
    }
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
