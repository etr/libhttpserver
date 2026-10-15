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
#error "hpack_field_section.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HPACK_FIELD_SECTION_HPP_
#define SRC_HTTPSERVER_DETAIL_HPACK_FIELD_SECTION_HPP_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <httpserver/detail/hpack_dynamic_table.hpp>
#include <httpserver/http/fields.hpp>

namespace httpserver::detail {
enum class hpack_indexing { incremental, without_indexing, never_indexed };
struct hpack_field_view {
    std::string_view name;
    std::string_view value;
    hpack_indexing indexing = hpack_indexing::incremental;
};
struct hpack_field {
    std::string name;
    std::string value;
    hpack_indexing indexing = hpack_indexing::incremental;
    hpack_field_view view() const noexcept { return {name, value, indexing}; }
};
struct hpack_section_limits {
    std::size_t max_compressed_bytes;
    std::size_t max_expanded_bytes;
    std::size_t max_fields;
};
struct hpack_section_result {
    hpack_status status;
    std::vector<hpack_field> fields;
    http::fields materialize() const;
};
inline std::span<const std::uint8_t> hpack_octets(std::string_view value) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}

// Allocation-free admission uses the primitive length parser and Huffman walk.
// value is the decoded length; consumed includes the string's prefix.
hpack_integer_result hpack_inspect_string(std::span<const std::uint8_t> input, hpack_string_limits limits) noexcept;
bool hpack_admit_field(std::size_t name, std::size_t value, std::size_t& remaining) noexcept;
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_FIELD_SECTION_HPP_
