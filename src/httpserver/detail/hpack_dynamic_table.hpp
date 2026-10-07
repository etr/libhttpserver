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
#error "hpack_dynamic_table.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HPACK_DYNAMIC_TABLE_HPP_
#define SRC_HTTPSERVER_DETAIL_HPACK_DYNAMIC_TABLE_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <httpserver/detail/hpack_primitives.hpp>
#include <httpserver/detail/hpack_static_table.hpp>
#include <httpserver/server/budgets.hpp>

namespace httpserver::detail {
inline bool hpack_entry_size(std::size_t name, std::size_t value, std::size_t& out) noexcept {
    if (name > SIZE_MAX - 32 || value > SIZE_MAX - 32 - name) return false;
    out = name + value + 32;
    return true;
}

// Synchronous, connection-serialized storage. Input strings own their octets
// before eviction, including a name borrowed from an entry about to be evicted.
class hpack_dynamic_table {
 public:
    explicit hpack_dynamic_table(server::resource_budget budget, std::size_t capacity = 4096)
        : budget_(std::move(budget)), capacity_(capacity) {}
    hpack_dynamic_table(const hpack_dynamic_table&) = delete;
    hpack_dynamic_table& operator=(const hpack_dynamic_table&) = delete;

    // Dynamic views are invalidated by insertion, eviction, or destruction.
    std::optional<hpack_static_entry> lookup(std::uint64_t index) const noexcept {
        if (const auto* entry = hpack_static_lookup(index)) return *entry;
        if (index < 62 || index - 62 >= entries_.size()) return std::nullopt;
        const auto& entry = entries_[static_cast<std::size_t>(index - 62)];
        return hpack_static_entry{entry.name, entry.value};
    }
    std::uint64_t find(std::string_view name, std::string_view value) const noexcept {
        if (const auto index = hpack_static_find(name, value)) return index;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].name == name && entries_[i].value == value) return i + 62;
        }
        return 0;
    }
    std::uint64_t find_name(std::string_view name) const noexcept {
        if (const auto index = hpack_static_find_name(name)) return index;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].name == name) return i + 62;
        }
        return 0;
    }
    hpack_status insert(std::string name, std::string value) {
        const auto name_size = name.size();
        const auto value_size = value.size();
        return insert_entry(name_size, value_size, [&](server::reservation charge) {
            return entry{std::move(name), std::move(value), std::move(charge)};
        });
    }
    // Views must refer to externally owned fields, not table storage. Admission
    // precedes string copies; use insert with owned strings for table references.
    hpack_status insert_copy(std::string_view name, std::string_view value) {
        return insert_entry(name.size(), value.size(), [&](server::reservation charge) {
            return entry{std::string(name), std::string(value), std::move(charge)};
        });
    }
    void set_capacity(std::size_t capacity) noexcept {
        capacity_ = capacity;
        evict_to(capacity);
    }
    void clear() noexcept { entries_.clear(); bytes_ = 0; }
    std::size_t bytes() const noexcept { return bytes_; }
    std::size_t size() const noexcept { return entries_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }

 private:
    struct entry {
        std::string name;
        std::string value;
        server::reservation charge;
    };
    template<typename MakeEntry>
    hpack_status insert_entry(std::size_t name, std::size_t value, MakeEntry make_entry) {
        std::size_t size = 0;
        if (!hpack_entry_size(name, value, size)) return {hpack_state::limit_exceeded};
        if (size > capacity_) {
            clear();
            return {};
        }
        evict_to(capacity_ - size);
        server::reservation charge;
        if (!budget_.reserve(server::resource::hpack_table_bytes, size, charge).ok()) return {hpack_state::limit_exceeded};
        entries_.insert(entries_.begin(), make_entry(std::move(charge)));
        bytes_ += size;
        return {};
    }
    void evict_to(std::size_t limit) noexcept {
        while (bytes_ > limit) {
            bytes_ -= entries_.back().charge.units();
            entries_.pop_back();
        }
    }
    server::resource_budget budget_;
    std::size_t capacity_;
    std::size_t bytes_ = 0;
    std::vector<entry> entries_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HPACK_DYNAMIC_TABLE_HPP_
