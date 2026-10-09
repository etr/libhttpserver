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
#error "qpack_decoder.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QPACK_DECODER_HPP_
#define SRC_HTTPSERVER_DETAIL_QPACK_DECODER_HPP_
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <httpserver/detail/qpack_field_section.hpp>
#include <httpserver/detail/qpack_dynamic_table.hpp>
namespace httpserver::detail {
// Input and any supplied table must stay immutable for both decode passes.
// The connection owner serializes table mutations; the decoder retains none.
class qpack_decoder {
 public:
    struct section_prefix {
        qpack_status status;
        std::uint64_t required = 0;
        std::uint64_t base = 0;
        std::size_t consumed = 0;
    };
    static section_prefix inspect_prefix(std::span<const std::uint8_t> input, const qpack_dynamic_table& table) noexcept {
        const auto encoded = qpack_decode_integer(input, 8, {});
        if (!encoded.status.ok()) return {encoded.status};
        std::uint64_t required = 0;
        if (encoded.value != 0) {
            const auto max_entries = table.maximum_capacity() / 32;
            const auto full_range = 2 * max_entries;
            if (max_entries == 0 || encoded.value > full_range || table.insert_count() > qpack_integer_maximum - max_entries) {
                return {{qpack_state::malformed}};
            }
            const auto max_value = table.insert_count() + max_entries;
            const auto max_wrapped = (max_value / full_range) * full_range;
            if (encoded.value - 1 > qpack_integer_maximum - max_wrapped) return {{qpack_state::malformed}};
            required = max_wrapped + encoded.value - 1;
            if (required > max_value) {
                if (required <= full_range) return {{qpack_state::malformed}};
                required -= full_range;
            }
            if (required == 0) return {{qpack_state::malformed}};
        }
        const auto base_wire = input.subspan(encoded.consumed);
        const auto delta = qpack_decode_integer(base_wire, 7, {});
        if (!delta.status.ok()) return {delta.status, required};
        std::uint64_t base;
        if ((base_wire[0] & 0x80) != 0) {
            if (delta.value >= required) return {{qpack_state::malformed}, required};
            base = required - delta.value - 1;
        } else {
            if (delta.value > qpack_integer_maximum - required) return {{qpack_state::malformed}, required};
            base = required + delta.value;
        }
        return {{}, required, base, encoded.consumed + delta.consumed};
    }
    qpack_section_result decode(std::span<const std::uint8_t> input, qpack_section_limits limits) const {
        try {
            return decode_allocated(input, limits);
        } catch (const std::bad_alloc&) {
            return {{qpack_state::limit_exceeded}, {}};
        } catch (const std::length_error&) {
            return {{qpack_state::limit_exceeded}, {}};
        }
    }
    // Owners with precharged storage distinguish allocation failure from peer
    // limits. Wire validation remains the same allocation-free first pass.
    qpack_section_result decode_allocated(std::span<const std::uint8_t> input, qpack_section_limits limits) const {
        if (input.size() > limits.max_compressed_bytes) return {{qpack_state::limit_exceeded}, {}};
        std::size_t count = 0;
        auto status = process(input, limits, nullptr, count);
        if (!status.ok()) return {complete(status), {}};
        qpack_section_result result;
        result.fields.reserve(count);
        status = process(input, limits, &result.fields, count);
        if (!status.ok()) return {complete(status), {}};
        return result;
    }

    qpack_section_result decode_dynamic(std::span<const std::uint8_t> input, const qpack_dynamic_table& table,
                                        qpack_section_limits limits) const {
        if (!table.initialized() || table.role() != qpack_table_role::decoder) return {{qpack_state::invalid_argument}, {}};
        if (input.size() > limits.max_compressed_bytes) return {{qpack_state::limit_exceeded}, {}};
        try {
            return decode_dynamic_allocated(input, table, limits, inspect_prefix(input, table));
        } catch (const std::bad_alloc&) { return {{qpack_state::limit_exceeded}, {}};
        } catch (const std::length_error&) { return {{qpack_state::limit_exceeded}, {}}; }
    }
    qpack_section_result decode_dynamic_allocated(std::span<const std::uint8_t> input, const qpack_dynamic_table& table,
                                                 qpack_section_limits limits, section_prefix section) const {
        if (!section.status.ok()) return {complete(section.status), {}, section.required};
        if (section.required > table.insert_count()) return {{qpack_state::inserts_unavailable}, {}, section.required};
        std::size_t count = 0;
        auto status = process(input, limits, nullptr, count, &table, &section);
        if (!status.ok()) return {complete(status), {}, section.required};
        qpack_section_result result;
        result.required_insert_count = section.required;
        result.fields.reserve(count);
        status = process(input, limits, &result.fields, count, &table, &section);
        if (!status.ok()) return {complete(status), {}, section.required};
        return result;
    }

 private:
    struct wire_field {
        const qpack_static_entry* entry = nullptr;
        const qpack_dynamic_entry* dynamic = nullptr;
        std::span<const std::uint8_t> name_wire;
        std::span<const std::uint8_t> value_wire;
        std::size_t name_size = 0;
        std::size_t value_size = 0;
        std::size_t consumed = 0;
        bool indexed = false;
        bool never_indexed = false;
    };
    static qpack_status complete(qpack_status status) noexcept {
        return status.state == qpack_state::incomplete ? qpack_status{qpack_state::malformed} : status;
    }
    static qpack_integer_result prefix(std::span<const std::uint8_t> input) noexcept {
        const auto required = qpack_decode_integer(input, 8, {});
        if (!required.status.ok()) return required;
        if (required.value != 0) return {{qpack_state::malformed}};
        const auto base_wire = input.subspan(required.consumed);
        const auto base = qpack_decode_integer(base_wire, 7, {});
        if (!base.status.ok()) return base;
        // With Required Insert Count zero, the sign would make Base negative.
        if ((base_wire[0] & 0x80) != 0) return {{qpack_state::malformed}};
        return {{}, 0, required.consumed + base.consumed};
    }
    static qpack_status read_name(std::span<const std::uint8_t> input, std::size_t remaining, std::size_t encoded_limit, wire_field& field,
                                  const qpack_dynamic_table* table, std::uint64_t base, std::uint64_t required) noexcept {
        const auto bits = input[0];
        if ((bits & 0x80) != 0 || (bits & 0x40) != 0) {
            field.indexed = (bits & 0x80) != 0;
            const auto static_bit = field.indexed ? 0x40 : 0x10;
            if ((bits & static_bit) == 0 && !table) return {qpack_state::malformed};
            const auto index = qpack_decode_integer(input, field.indexed ? 6 : 4, {});
            if (!index.status.ok()) return index.status;
            if (bits & static_bit) {
                field.entry = qpack_static_lookup(index.value);
                if (!field.entry) return {qpack_state::malformed};
                field.name_size = field.entry->name.size();
                field.value_size = field.indexed ? field.entry->value.size() : 0;
            } else {
                field.dynamic = table->section_relative(base, index.value);
                if (!field.dynamic || field.dynamic->absolute >= required) return {qpack_state::malformed};
                field.name_size = field.dynamic->name.size();
                field.value_size = field.indexed ? field.dynamic->value.size() : 0;
            }
            field.consumed = index.consumed;
            field.never_indexed = !field.indexed && (bits & 0x20) != 0;
        } else if ((bits & 0x20) != 0) {
            const auto name = qpack_inspect_string(input, {encoded_limit, remaining - 32}, 3, 8);
            if (!name.status.ok()) return name.status;
            field.name_wire = input.first(name.consumed);
            field.name_size = static_cast<std::size_t>(name.value);
            field.consumed = name.consumed;
            field.never_indexed = (bits & 0x10) != 0;
        } else {
            if (!table) return {qpack_state::malformed};
            field.indexed = (bits & 0x10) != 0;
            const auto index = qpack_decode_integer(input, field.indexed ? 4 : 3, {});
            if (!index.status.ok()) return index.status;
            field.dynamic = table->post_base(base, index.value);
            if (!field.dynamic || field.dynamic->absolute >= required) return {qpack_state::malformed};
            field.name_size = field.dynamic->name.size();
            field.value_size = field.indexed ? field.dynamic->value.size() : 0;
            field.consumed = index.consumed;
            field.never_indexed = !field.indexed && (bits & 8) != 0;
        }
        return {};
    }
    static qpack_status read_field(std::span<const std::uint8_t> input, std::size_t& remaining, std::size_t encoded_limit, wire_field& field,
                                    const qpack_dynamic_table* table, std::uint64_t base, std::uint64_t required) noexcept {
        if (remaining < 32) return {qpack_state::limit_exceeded};
        auto status = read_name(input, remaining, encoded_limit, field, table, base, required);
        if (!status.ok()) return status;
        if (field.name_size > remaining - 32) return {qpack_state::limit_exceeded};
        if (!field.indexed) {
            const auto value_wire = input.subspan(field.consumed);
            const auto value = qpack_inspect_string(value_wire, {encoded_limit, remaining - 32 - field.name_size});
            if (!value.status.ok()) return value.status;
            field.value_wire = value_wire.first(value.consumed);
            field.value_size = static_cast<std::size_t>(value.value);
            field.consumed += value.consumed;
        }
        return qpack_admit_field(field.name_size, field.value_size, remaining) ? qpack_status{} : qpack_status{qpack_state::limit_exceeded};
    }
    static qpack_field materialize(const wire_field& field) {
        qpack_field out;
        out.never_indexed = field.never_indexed;
        if (field.dynamic) {
            out.name = field.dynamic->name;
        } else if (field.entry) {
            out.name = field.entry->name;
        } else {
            out.name = qpack_decode_string(field.name_wire, {field.name_wire.size(), field.name_size}, 3, 8).value;
        }
        if (field.indexed) {
            out.value = field.dynamic ? field.dynamic->value : std::string(field.entry->value);
        } else {
            out.value = qpack_decode_string(field.value_wire, {field.value_wire.size(), field.value_size}).value;
        }
        return out;
    }
    static qpack_status process(std::span<const std::uint8_t> input, qpack_section_limits limits,
                                std::vector<qpack_field>* output, std::size_t& count, const qpack_dynamic_table* table = nullptr, const section_prefix* frozen = nullptr) {
        section_prefix section;
        if (frozen) {
            section = *frozen;
        } else if (table) {
            section = inspect_prefix(input, *table);
        } else {
            const auto legacy = prefix(input);
            section = {legacy.status, 0, 0, legacy.consumed};
        }
        if (!section.status.ok()) return section.status;
        input = input.subspan(section.consumed);
        auto remaining = limits.max_expanded_bytes;
        count = 0;
        std::uint64_t largest_required = 0;
        while (!input.empty()) {
            if (count == limits.max_fields) return {qpack_state::limit_exceeded};
            wire_field field;
            const auto status = read_field(input, remaining, limits.max_compressed_bytes, field, table, section.base, section.required);
            if (!status.ok()) return status;
            if (field.dynamic) largest_required = std::max(largest_required, field.dynamic->absolute + 1);
            if (output != nullptr) output->push_back(materialize(field));
            input = input.subspan(field.consumed);
            ++count;
        }
        if (table && largest_required != section.required) return {qpack_state::malformed};
        return {};
    }
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_DECODER_HPP_
