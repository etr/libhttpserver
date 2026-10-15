/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/qpack_encoder.hpp>

namespace httpserver::detail {

auto qpack_encoder::encode(std::span<const qpack_field_view> fields, qpack_section_limits limits, bool huffman) const -> qpack_bytes_result {
    return encode_sequence(fields.size(), [fields](auto i) { return fields[i]; }, limits, huffman);
}

auto qpack_encoder::encode_section(std::span<const qpack_field> fields, qpack_section_limits limits, bool huffman) const -> qpack_bytes_result {
    return encode_sequence(fields.size(), [fields](auto i) { return fields[i].view(); }, limits, huffman);
}

auto qpack_encoder::encode_fields(const http::fields& fields, bool never_indexed, qpack_section_limits limits, bool huffman) const -> qpack_bytes_result {
    return encode_sequence(
        fields.size(),
        [&fields, never_indexed](auto i) {
            const auto [key, value] = fields.order_[i];
            return qpack_field_view{fields.name_store_[key], fields.value_store_[key][value], never_indexed};
        },
        limits, huffman);
}

auto qpack_encoder::encode_dynamic(std::span<const qpack_field_view> fields, qpack_dynamic_table& table, std::uint64_t base, qpack_section_limits limits, bool huffman,
                                   std::optional<std::uint64_t> peer_maximum_capacity) const -> qpack_dynamic_bytes_result {
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
        // Wire modulo follows the peer's advertised maximum, independently
        // of the locally admitted table storage. Legacy callers use the table maximum.
        const auto max_entries = peer_maximum_capacity.value_or(table.maximum_capacity()) / 32;
        if (required && max_entries == 0) return {{qpack_state::invalid_argument}, {}, 0, {}};
        const auto encoded_required = required ? (required % (2 * max_entries)) + 1 : 0;
        auto prefix_bytes = qpack_encode_integer(encoded_required, 8, 0, {}, limits.max_compressed_bytes);
        if (!prefix_bytes.status.ok()) return {prefix_bytes.status, {}, 0, {}};
        auto wire = std::move(prefix_bytes.value);
        auto status = append(qpack_encode_integer(base >= required ? base - required : required - base - 1, 7, base >= required ? 0 : 0x80, {}, limits.max_compressed_bytes - wire.size()), wire);
        if (!status.ok()) return {status, {}, 0, {}};
        for (const auto& field : fields) {
            status = write_dynamic_field(field, table, base, huffman, wire, limits.max_compressed_bytes);
            if (!status.ok()) return {status, {}, 0, {}};
        }
        qpack_dynamic_bytes_result result;
        status = table.acquire(indexes, limits.max_fields, result.references);
        if (!status.ok()) return {status, {}, 0, {}};
        result.required_insert_count = required;
        result.value = std::move(wire);
        result.consumed = result.value.size();
        return result;
    } catch (const std::bad_alloc&) {
        return {{qpack_state::limit_exceeded}, {}, 0, {}};
    } catch (const std::length_error&) {
        return {{qpack_state::limit_exceeded}, {}, 0, {}};
    }
}

auto qpack_encoder::dynamic_reference(qpack_field_view field, const qpack_dynamic_table& table) noexcept -> std::optional<std::uint64_t> {
    if (!field.never_indexed && qpack_static_find(field.name, field.value)) return std::nullopt;
    if (!field.never_indexed) {
        if (auto exact = table.find(field.name, field.value)) return exact;
    }
    return table.find_name(field.name);
}

auto qpack_encoder::write_dynamic_field(qpack_field_view field, const qpack_dynamic_table& table, std::uint64_t base, bool huffman, std::string& wire, std::size_t limit) -> qpack_status {
    const auto dynamic = dynamic_reference(field, table);
    if (!dynamic) return write_field(field, huffman, wire, limit);
    const auto* entry = table.lookup(*dynamic);
    const bool indexed = !field.never_indexed && entry->value == field.value;
    const bool relative = *dynamic < base;
    const auto index = relative ? base - *dynamic - 1 : *dynamic - base;
    const unsigned prefix = indexed ? (relative ? 6 : 4) : (relative ? 4 : 3);
    const std::uint8_t bits = indexed ? (relative ? 0x80 : 0x10) : (relative ? (field.never_indexed ? 0x60 : 0x40) : (field.never_indexed ? 8 : 0));
    const auto status = append(qpack_encode_integer(index, prefix, bits, {}, limit - wire.size()), wire);
    if (!status.ok() || indexed) return status;
    return write_string(field.value, huffman, wire, limit);
}

auto qpack_encoder::append(qpack_bytes_result bytes, std::string& wire) -> qpack_status {
    if (!bytes.status.ok()) return bytes.status;
    wire += bytes.value;
    return {};
}

auto qpack_encoder::write_string(std::string_view value, bool huffman, std::string& wire, std::size_t limit, unsigned prefix, std::uint8_t flag, std::uint8_t bits) -> qpack_status {
    const auto remaining = limit - wire.size();
    return append(qpack_encode_string(qpack_octets(value), huffman, {remaining, value.size()}, remaining, prefix, flag, bits), wire);
}

auto qpack_encoder::write_field(qpack_field_view field, bool huffman, std::string& wire, std::size_t limit) -> qpack_status {
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

}  // namespace httpserver::detail
