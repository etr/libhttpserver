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
#error "qpack_dynamic_table.hpp is internal to libhttpserver"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QPACK_DYNAMIC_TABLE_HPP_
#define SRC_HTTPSERVER_DETAIL_QPACK_DYNAMIC_TABLE_HPP_
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <httpserver/detail/qpack_primitives.hpp>
#include <httpserver/server/budgets.hpp>
namespace httpserver::detail {
enum class qpack_table_role { encoder, decoder };
struct qpack_dynamic_entry {
    std::string name;
    std::string value;
    std::uint64_t absolute = 0;
    std::size_t bytes = 0;
    std::size_t references = 0;
};
// The table must outlive all holds. Stable nodes keep release independent of
// insertions, and pinned encoder entries cannot be removed.
class qpack_reference_hold {
 public:
    qpack_reference_hold() noexcept = default;
    qpack_reference_hold(const qpack_reference_hold&) = delete;
    qpack_reference_hold& operator=(const qpack_reference_hold&) = delete;
    qpack_reference_hold(qpack_reference_hold&& other) noexcept { entries_.swap(other.entries_); }
    qpack_reference_hold& operator=(qpack_reference_hold&& other) noexcept;
    ~qpack_reference_hold() { release(); }
    void release() noexcept;
    std::size_t size() const noexcept { return entries_.size(); }
 private:
    friend class qpack_dynamic_table;
    std::vector<qpack_dynamic_entry*> entries_;
};
// Connection-serialized storage. Reserve the configured maximum once: a
// replacement can prepare its strings before evicting without double-charging
// capacity. Codec output and section holds have their own explicit bounds.
class qpack_dynamic_table {
 public:
    explicit qpack_dynamic_table(qpack_table_role role) noexcept : role_(role) {}
    qpack_dynamic_table(const qpack_dynamic_table&) = delete;
    qpack_dynamic_table& operator=(const qpack_dynamic_table&) = delete;
    qpack_dynamic_table(qpack_dynamic_table&&) = delete;
    qpack_dynamic_table& operator=(qpack_dynamic_table&&) = delete;
    qpack_status initialize(const server::resource_budget& budget, std::size_t maximum);
    // Connection teardown calls this after destroying every reference hold.
    void clear() noexcept;
    bool initialized() const noexcept { return initialized_; }
    qpack_table_role role() const noexcept { return role_; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t maximum_capacity() const noexcept { return maximum_; }
    std::size_t bytes() const noexcept { return bytes_; }
    std::size_t size() const noexcept { return entries_.size(); }
    std::uint64_t insert_count() const noexcept { return inserts_; }
    std::uint64_t known_received_count() const noexcept { return received_; }
    // Returned views survive until eviction or table destruction.
    const qpack_dynamic_entry* lookup(std::uint64_t absolute) const noexcept;
    std::optional<std::uint64_t> find(std::string_view name, std::string_view value) const noexcept;
    std::optional<std::uint64_t> find_name(std::string_view name) const noexcept;
    const qpack_dynamic_entry* encoder_relative(std::uint64_t index) const noexcept { return section_relative(inserts_, index); }
    const qpack_dynamic_entry* section_relative(std::uint64_t base, std::uint64_t index) const noexcept {
        return base <= qpack_integer_maximum && index < base ? lookup(base - index - 1) : nullptr;
    }
    const qpack_dynamic_entry* post_base(std::uint64_t base, std::uint64_t index) const noexcept {
        return base <= qpack_integer_maximum && index <= qpack_integer_maximum - base ? lookup(base + index) : nullptr;
    }
    qpack_status set_capacity(std::size_t capacity) noexcept;
    qpack_status insert(std::string_view name, std::string_view value);
    qpack_status insert_allocated(std::string_view name, std::string_view value);
    qpack_status duplicate(std::uint64_t relative);

    qpack_status advance_known_received(std::uint64_t count) noexcept;
    qpack_status increment_known_received(std::uint64_t count) noexcept;
    qpack_status acquire(std::span<const std::uint64_t> indexes, std::size_t maximum, qpack_reference_hold& out);

 private:
    qpack_status can_evict_to(std::size_t limit) const noexcept;
    void evict_to(std::size_t limit) noexcept;
    const qpack_table_role role_;
    bool initialized_ = false;
    server::reservation charge_;
    std::size_t maximum_ = 0;
    std::size_t capacity_ = 0;
    std::size_t bytes_ = 0;
    std::uint64_t inserts_ = 0;
    std::uint64_t received_ = 0;
    std::list<qpack_dynamic_entry> entries_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QPACK_DYNAMIC_TABLE_HPP_
