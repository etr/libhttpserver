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

#if !defined(HTTPSERVER_COMPILATION)
#error "hpack_encoder.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HPACK_ENCODER_HPP_
#define SRC_HTTPSERVER_DETAIL_HPACK_ENCODER_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <httpserver/detail/hpack_field_section.hpp>

namespace httpserver::detail {
// Encode at the final transmission-order boundary. A successful block must be
// sent without dropping/reordering it; otherwise close the connection. All
// operations require the connection's serialized execution boundary. Input
// octets must remain owned outside this encoder for the entire call.
class hpack_encoder {
 public:
    explicit hpack_encoder(server::resource_budget budget) : table_(std::move(budget)) {}
    bool usable() const noexcept { return usable_; }
    const hpack_dynamic_table& table() const noexcept { return table_; }
    std::size_t peer_maximum() const noexcept { return peer_maximum_; }
    hpack_status set_peer_maximum(std::uint64_t maximum) noexcept {
        if (maximum > UINT32_MAX) return {hpack_state::invalid_argument};
        if (!usable_) return {hpack_state::malformed};
        peer_maximum_ = static_cast<std::size_t>(maximum);
        return choose_capacity(std::min(table_.capacity(), peer_maximum_));
    }
    hpack_status choose_capacity(std::uint64_t capacity) noexcept {
        if (capacity > peer_maximum_) return {hpack_state::invalid_argument};
        if (!usable_) return {hpack_state::malformed};
        if (capacity != table_.capacity()) {
            const auto chosen = static_cast<std::size_t>(capacity);
            pending_minimum_ = std::min(pending_minimum_.value_or(chosen), chosen);
            table_.set_capacity(chosen);
        }
        return {};
    }
    hpack_bytes_result encode(std::span<const hpack_field_view> fields, hpack_section_limits limits, bool huffman = false) {
        return encode_sequence(fields, [](auto field) { return field; }, limits, huffman);
    }
    hpack_bytes_result encode_section(std::span<const hpack_field> fields, hpack_section_limits limits, bool huffman = false) {
        return encode_sequence(fields, [](const auto& field) { return field.view(); }, limits, huffman);
    }
    // Semantic fields do not retain sensitivity. Policy is always explicit;
    // use encode_section when relaying decoded never-indexed occurrences.
    hpack_bytes_result encode_fields(const http::fields& fields, hpack_indexing policy, hpack_section_limits limits, bool huffman = false) {
        if (!valid_indexing(policy)) return {{hpack_state::invalid_argument}, {}};
        if (!usable_) return {{hpack_state::malformed}, {}};
        if (fields.size() > limits.max_fields) return fail({hpack_state::limit_exceeded});
        return encode_sequence(fields.entries(), [policy](auto field) {
            return hpack_field_view{field.name, field.value, policy};
        }, limits, huffman);
    }

 private:
    static bool valid_indexing(hpack_indexing indexing) noexcept {
        return indexing == hpack_indexing::incremental || indexing == hpack_indexing::without_indexing || indexing == hpack_indexing::never_indexed;
    }
    template<typename Range, typename View>
    hpack_bytes_result encode_sequence(const Range& fields, View view, hpack_section_limits limits, bool huffman) {
        // Validate every policy before emitting pending updates or changing tables.
        for (const auto& field : fields) {
            if (!valid_indexing(view(field).indexing)) return {{hpack_state::invalid_argument}, {}};
        }
        if (!usable_) return {{hpack_state::malformed}, {}};
        try {
            return process(fields, view, limits, huffman);
        } catch (const std::bad_alloc&) {
            return fail({hpack_state::limit_exceeded});
        } catch (const std::length_error&) {
            return fail({hpack_state::limit_exceeded});
        }
    }
    template<typename Range, typename View>
    hpack_bytes_result process(const Range& fields, View view, hpack_section_limits limits, bool huffman) {
        if (fields.size() > limits.max_fields) return fail({hpack_state::limit_exceeded});
        std::string wire;
        auto status = updates(wire, limits.max_compressed_bytes);
        if (!status.ok()) return fail(status);
        auto remaining = limits.max_expanded_bytes;
        for (const auto& occurrence : fields) {
            const auto field = view(occurrence);
            if (!hpack_admit_field(field.name.size(), field.value.size(), remaining)) return fail({hpack_state::limit_exceeded});
            status = write_field(field, huffman, wire, limits.max_compressed_bytes);
            if (!status.ok()) return fail(status);
        }
        pending_minimum_.reset();
        const auto size = wire.size();
        return {{}, std::move(wire), size};
    }
    hpack_status write_string(std::string_view value, bool huffman, std::string& wire, std::size_t limit) {
        const auto remaining = limit - wire.size();
        return append(hpack_encode_string(hpack_octets(value), huffman, {remaining, value.size()}, remaining), wire);
    }
    hpack_status write_field(hpack_field_view field, bool huffman, std::string& wire, std::size_t limit) {
        const auto exact = table_.find(field.name, field.value);
        if (exact != 0 && field.indexing != hpack_indexing::never_indexed) {
            return append(hpack_encode_integer(exact, 7, 0x80, {}, limit - wire.size()), wire);
        }
        return write_literal(field, huffman, wire, limit);
    }
    hpack_status write_literal(hpack_field_view field, bool huffman, std::string& wire, std::size_t limit) {
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
    hpack_bytes_result fail(hpack_status status) noexcept {
        usable_ = false;
        table_.clear();
        return {status, {}};
    }
    static hpack_status append(hpack_bytes_result bytes, std::string& wire) {
        if (!bytes.status.ok()) return bytes.status;
        wire += bytes.value;
        return {};
    }
    hpack_status updates(std::string& wire, std::size_t limit) {
        if (!pending_minimum_) return {};
        if (*pending_minimum_ < table_.capacity()) {
            const auto status = append(hpack_encode_integer(*pending_minimum_, 5, 0x20, {}, limit - wire.size()), wire);
            if (!status.ok()) return status;
        }
        return append(hpack_encode_integer(table_.capacity(), 5, 0x20, {}, limit - wire.size()), wire);
    }
    hpack_dynamic_table table_;
    std::size_t peer_maximum_ = 4096;
    std::optional<std::size_t> pending_minimum_;
    bool usable_ = true;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_ENCODER_HPP_
