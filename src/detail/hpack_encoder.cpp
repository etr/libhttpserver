/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <string>

#include <httpserver/detail/hpack_encoder.hpp>

namespace httpserver::detail {

auto hpack_encoder::set_peer_maximum(std::uint64_t maximum) noexcept -> hpack_status {
    if (maximum > UINT32_MAX) return {hpack_state::invalid_argument};
    if (!usable_) return {hpack_state::malformed};
    peer_maximum_ = static_cast<std::size_t>(maximum);
    return choose_capacity(std::min(table_.capacity(), peer_maximum_));
}

auto hpack_encoder::choose_capacity(std::uint64_t capacity) noexcept -> hpack_status {
    if (capacity > peer_maximum_) return {hpack_state::invalid_argument};
    if (!usable_) return {hpack_state::malformed};
    if (capacity != table_.capacity()) {
        const auto chosen = static_cast<std::size_t>(capacity);
        pending_minimum_ = std::min(pending_minimum_.value_or(chosen), chosen);
        table_.set_capacity(chosen);
    }
    return {};
}

auto hpack_encoder::encode(std::span<const hpack_field_view> fields, hpack_section_limits limits, bool huffman) -> hpack_bytes_result {
    return encode_sequence(fields, [](auto field) { return field; }, limits, huffman);
}

auto hpack_encoder::encode_section(std::span<const hpack_field> fields, hpack_section_limits limits, bool huffman) -> hpack_bytes_result {
    return encode_sequence(fields, [](const auto& field) { return field.view(); }, limits, huffman);
}

auto hpack_encoder::encode_fields(const http::fields& fields, hpack_indexing policy, hpack_section_limits limits, bool huffman) -> hpack_bytes_result {
    if (!valid_indexing(policy)) return {{hpack_state::invalid_argument}, {}};
    if (!usable_) return {{hpack_state::malformed}, {}};
    if (fields.size() > limits.max_fields) return fail({hpack_state::limit_exceeded});
    return encode_sequence(fields.entries(), [policy](auto field) { return hpack_field_view{field.name, field.value, policy}; }, limits, huffman);
}

auto hpack_encoder::valid_indexing(hpack_indexing indexing) noexcept -> bool {
    return indexing == hpack_indexing::incremental || indexing == hpack_indexing::without_indexing || indexing == hpack_indexing::never_indexed;
}

auto hpack_encoder::write_string(std::string_view value, bool huffman, std::string& wire, std::size_t limit) -> hpack_status {
    const auto remaining = limit - wire.size();
    return append(hpack_encode_string(hpack_octets(value), huffman, {remaining, value.size()}, remaining), wire);
}

auto hpack_encoder::write_field(hpack_field_view field, bool huffman, std::string& wire, std::size_t limit) -> hpack_status {
    const auto exact = table_.find(field.name, field.value);
    if (exact != 0 && field.indexing != hpack_indexing::never_indexed) {
        return append(hpack_encode_integer(exact, 7, 0x80, {}, limit - wire.size()), wire);
    }
    return write_literal(field, huffman, wire, limit);
}

auto hpack_encoder::write_literal(hpack_field_view field, bool huffman, std::string& wire, std::size_t limit) -> hpack_status {
    const bool incremental = field.indexing == hpack_indexing::incremental;
    const auto bits = incremental ? 0x40 : field.indexing == hpack_indexing::never_indexed ? 0x10 : 0;
    const auto name = table_.find_name(field.name);
    auto status = append(hpack_encode_integer(name, incremental ? 6 : 4, bits, {}, limit - wire.size()), wire);
    if (!status.ok()) return status;
    if (name == 0) {
        status = write_string(field.name, huffman, wire, limit);
        if (!status.ok()) return status;
    }
    status = write_string(field.value, huffman, wire, limit);
    if (!status.ok()) return status;
    return incremental ? table_.insert_copy(field.name, field.value) : hpack_status{};
}

auto hpack_encoder::fail(hpack_status status) noexcept -> hpack_bytes_result {
    usable_ = false;
    table_.clear();
    return {status, {}};
}

auto hpack_encoder::append(hpack_bytes_result bytes, std::string& wire) -> hpack_status {
    if (!bytes.status.ok()) return bytes.status;
    wire += bytes.value;
    return {};
}

auto hpack_encoder::updates(std::string& wire, std::size_t limit) -> hpack_status {
    if (!pending_minimum_) return {};
    if (*pending_minimum_ < table_.capacity()) {
        const auto status = append(hpack_encode_integer(*pending_minimum_, 5, 0x20, {}, limit - wire.size()), wire);
        if (!status.ok()) return status;
    }
    return append(hpack_encode_integer(table_.capacity(), 5, 0x20, {}, limit - wire.size()), wire);
}

}  // namespace httpserver::detail
