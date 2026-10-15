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
#error "hpack_decoder.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HPACK_DECODER_HPP_
#define SRC_HTTPSERVER_DETAIL_HPACK_DECODER_HPP_

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
// Complete blocks must arrive in received connection order under the caller's
// serialized execution boundary. Any processing failure requires closure;
// partially consumed compression state cannot be retried or rolled back.
class hpack_decoder {
 public:
    explicit hpack_decoder(server::resource_budget budget) : table_(std::move(budget)) {}
    // Called only when the local SETTINGS maximum has been acknowledged.
    // A memory ceiling belongs to the resource budget, never this wire setting.
    hpack_status acknowledge_maximum(std::uint64_t maximum) noexcept;
    std::size_t acknowledged_maximum() const noexcept { return maximum_; }
    bool usable() const noexcept { return usable_; }
    const hpack_dynamic_table& table() const noexcept { return table_; }
    hpack_section_result decode(std::span<const std::uint8_t> wire, hpack_section_limits limits);

 private:
    hpack_section_result fail(hpack_status status) noexcept;
    hpack_status indexed(std::span<const std::uint8_t>& wire, std::size_t& remaining, hpack_field& field);
    hpack_status literal(std::span<const std::uint8_t>& wire, std::size_t& remaining, hpack_field& field);
    static hpack_status read_literal(std::optional<hpack_static_entry> entry, std::span<const std::uint8_t>& wire,
                                    std::size_t& remaining, hpack_field& field);
    hpack_status size_update(std::span<const std::uint8_t>& wire) noexcept;
    hpack_section_result process(std::span<const std::uint8_t> wire, hpack_section_limits limits);
    hpack_dynamic_table table_;
    std::size_t maximum_ = 4096;
    std::optional<std::size_t> required_minimum_;
    bool usable_ = true;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_DECODER_HPP_
