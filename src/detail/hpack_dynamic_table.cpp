/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/hpack_dynamic_table.hpp>
#include <string>
#include <utility>

namespace httpserver::detail {

bool hpack_entry_size(std::size_t name, std::size_t value, std::size_t& out) noexcept {
    if (name > SIZE_MAX - 32 || value > SIZE_MAX - 32 - name) return false;
    out = name + value + 32;
    return true;
}

auto hpack_dynamic_table::lookup(std::uint64_t index) const noexcept -> std::optional<hpack_static_entry> {
    if (const auto* entry = hpack_static_lookup(index)) return *entry;
    if (index < 62 || index - 62 >= entries_.size()) return std::nullopt;
    const auto& entry = entries_[static_cast<std::size_t>(index - 62)];
    return hpack_static_entry{entry.name, entry.value};
}

auto hpack_dynamic_table::find(std::string_view name, std::string_view value) const noexcept -> std::uint64_t {
    if (const auto index = hpack_static_find(name, value)) return index;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name == name && entries_[i].value == value) return i + 62;
    }
    return 0;
}

auto hpack_dynamic_table::find_name(std::string_view name) const noexcept -> std::uint64_t {
    if (const auto index = hpack_static_find_name(name)) return index;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name == name) return i + 62;
    }
    return 0;
}

auto hpack_dynamic_table::insert(std::string name, std::string value) -> hpack_status {
    const auto name_size = name.size();
    const auto value_size = value.size();
    return insert_entry(name_size, value_size, [&](server::reservation charge) { return entry{std::move(name), std::move(value), std::move(charge)}; });
}

auto hpack_dynamic_table::insert_copy(std::string_view name, std::string_view value) -> hpack_status {
    return insert_entry(name.size(), value.size(), [&](server::reservation charge) { return entry{std::string(name), std::string(value), std::move(charge)}; });
}

auto hpack_dynamic_table::set_capacity(std::size_t capacity) noexcept -> void {
    capacity_ = capacity;
    evict_to(capacity);
}

auto hpack_dynamic_table::clear() noexcept -> void {
    entries_.clear();
    bytes_ = 0;
}

auto hpack_dynamic_table::evict_to(std::size_t limit) noexcept -> void {
    while (bytes_ > limit) {
        bytes_ -= entries_.back().charge.units();
        entries_.pop_back();
    }
}

}  // namespace httpserver::detail
