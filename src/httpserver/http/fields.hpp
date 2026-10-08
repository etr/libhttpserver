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

#ifndef SRC_HTTPSERVER_HTTP_FIELDS_HPP_
#define SRC_HTTPSERVER_HTTP_FIELDS_HPP_

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace httpserver {

namespace detail { class qpack_encoder; }

namespace http {

// Insertion-ordered multivalue field collection (PRD-V3N-REQ-017/018).
// Headers and trailers preserve wire order and repeated names: append
// keeps existing entries and adds a new one at the end of the
// sequence; replace erases every entry for the name (any case) and
// appends a single new entry at the end of the sequence; first
// returns the earliest received occurrence; all returns the values in
// received order.
//
// Name lookup is case-insensitive, but the first-seen spelling of a
// name is preserved and exposed by entries() so the sequence can be
// re-emitted on the wire as received.
//
// The type is a deep-copyable value type. Views returned by first(),
// all(), and entries() refer to storage owned by the container and are
// invalidated by any subsequent mutation.
class fields {
 public:
    // One received field occurrence. The views refer to container
    // storage and are invalidated by mutation.
    struct entry {
        std::string_view name;
        std::string_view value;
    };

    // Adds a new occurrence at the end of the sequence, keeping any
    // existing entries for the name.
    void append(std::string_view name, std::string_view value) {
        const std::size_t key = find_or_add_key(name);
        value_store_[key].emplace_back(value);
        order_.emplace_back(key, value_store_[key].size() - 1);
        cache_dirty_ = true;
    }

    // Erases every entry for the name (any case), then appends a
    // single entry at the end of the sequence. The first-seen spelling
    // of the name is retained.
    void replace(std::string_view name, std::string_view value) {
        const std::size_t key = find_or_add_key(name);
        erase_order_for(key);
        value_store_[key].clear();
        value_store_[key].emplace_back(value);
        order_.emplace_back(key, 0);
        cache_dirty_ = true;
    }

    // Erases every entry for the name (any case). Returns true iff any
    // entry was removed.
    bool remove(std::string_view name) {
        const std::size_t key = find_key(name);
        if (key == npos || value_store_[key].empty()) return false;
        erase_order_for(key);
        value_store_[key].clear();
        cache_dirty_ = true;
        return true;
    }

    // Earliest received occurrence of the name, or nullopt.
    std::optional<std::string_view> first(std::string_view name) const {
        const std::size_t key = find_key(name);
        if (key == npos || value_store_[key].empty()) return std::nullopt;
        return std::optional<std::string_view>(value_store_[key].front());
    }

    // All occurrences of the name in received order; empty span when
    // the name is absent.
    std::span<const std::string> all(std::string_view name) const {
        static constexpr std::span<const std::string> none;
        const std::size_t key = find_key(name);
        if (key == npos) return none;
        return value_store_[key];
    }

    // Number of occurrences of the name.
    std::size_t count(std::string_view name) const {
        const std::size_t key = find_key(name);
        return key == npos ? 0 : value_store_[key].size();
    }

    // The full sequence in insertion (wire) order.
    std::span<const entry> entries() const noexcept {
        if (cache_dirty_) rebuild_cache();
        return entry_cache_;
    }

    bool empty() const noexcept { return order_.empty(); }

    // Total number of field occurrences across all names.
    std::size_t size() const noexcept { return order_.size(); }

    // Entry-sequence equality: same number of occurrences in the same
    // order, with equal (case-insensitive) names and equal values at
    // every position.
    friend bool operator==(const fields& a, const fields& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.order_.size(); ++i) {
            const auto& [ak, av] = a.order_[i];
            const auto& [bk, bv] = b.order_[i];
            if (!iequals(a.name_store_[ak], b.name_store_[bk])) return false;
            if (a.value_store_[ak][av] != b.value_store_[bk][bv]) return false;
        }
        return true;
    }

    friend bool operator!=(const fields& a, const fields& b) {
        return !(a == b);
    }

 private:
    // The private codec traverses owned occurrences without allocating the entry cache.
    friend class httpserver::detail::qpack_encoder;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    static bool iequals(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
        }
        return true;
    }

    static constexpr char ascii_lower(const char c) noexcept {
        const auto u = static_cast<unsigned char>(c);
        return (u >= 'A' && u <= 'Z')
                   ? static_cast<char>(u - 'A' + 'a')
                   : c;
    }

    std::size_t find_key(std::string_view name) const noexcept {
        for (std::size_t i = 0; i < name_store_.size(); ++i) {
            if (iequals(name_store_[i], name)) return i;
        }
        return npos;
    }

    std::size_t find_or_add_key(std::string_view name) {
        const std::size_t existing = find_key(name);
        if (existing != npos) return existing;
        name_store_.emplace_back(name);
        value_store_.emplace_back();
        return name_store_.size() - 1;
    }

    void erase_order_for(std::size_t key) {
        order_.erase(std::remove_if(order_.begin(), order_.end(),
                                    [key](const auto& slot) {
                                        return slot.first == key;
                                    }),
                     order_.end());
    }

    void rebuild_cache() const {
        entry_cache_.clear();
        entry_cache_.reserve(order_.size());
        for (const auto& [key, value_idx] : order_) {
            entry_cache_.push_back(entry{name_store_[key],
                                         value_store_[key][value_idx]});
        }
        cache_dirty_ = false;
    }

    // Distinct names in first-seen order (first-seen spelling kept),
    // with one value list per name in received order.
    std::vector<std::string> name_store_;
    std::vector<std::vector<std::string>> value_store_;
    // (name key, value index) per occurrence, in insertion order.
    std::vector<std::pair<std::size_t, std::size_t>> order_;
    // Lazily rebuilt views for entries(); invalidated by mutation.
    mutable std::vector<entry> entry_cache_;
    mutable bool cache_dirty_ = true;
};

}  // namespace http

}  // namespace httpserver

#endif  // SRC_HTTPSERVER_HTTP_FIELDS_HPP_
