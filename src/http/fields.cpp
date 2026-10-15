/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/http/fields.hpp>
#include <string>

namespace httpserver::http {

auto fields::append(std::string_view name, std::string_view value) -> void {
    const std::size_t key = find_or_add_key(name);
    value_store_[key].emplace_back(value);
    order_.emplace_back(key, value_store_[key].size() - 1);
    cache_dirty_ = true;
}

auto fields::replace(std::string_view name, std::string_view value) -> void {
    const std::size_t key = find_or_add_key(name);
    erase_order_for(key);
    value_store_[key].clear();
    value_store_[key].emplace_back(value);
    order_.emplace_back(key, 0);
    cache_dirty_ = true;
}

auto fields::remove(std::string_view name) -> bool {
    const std::size_t key = find_key(name);
    if (key == npos || value_store_[key].empty()) return false;
    erase_order_for(key);
    value_store_[key].clear();
    cache_dirty_ = true;
    return true;
}

auto fields::first(std::string_view name) const -> std::optional<std::string_view> {
    const std::size_t key = find_key(name);
    if (key == npos || value_store_[key].empty()) return std::nullopt;
    return std::optional<std::string_view>(value_store_[key].front());
}

auto fields::all(std::string_view name) const -> std::span<const std::string> {
    static constexpr std::span<const std::string> none;
    const std::size_t key = find_key(name);
    if (key == npos) return none;
    return value_store_[key];
}

auto fields::count(std::string_view name) const -> std::size_t {
    const std::size_t key = find_key(name);
    return key == npos ? 0 : value_store_[key].size();
}

auto fields::entries() const -> std::span<const entry> {
    if (cache_dirty_) rebuild_cache();
    return entry_cache_;
}

auto fields::iequals(std::string_view a, std::string_view b) noexcept -> bool {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

auto fields::find_key(std::string_view name) const noexcept -> std::size_t {
    for (std::size_t i = 0; i < name_store_.size(); ++i) {
        if (iequals(name_store_[i], name)) return i;
    }
    return npos;
}

auto fields::find_or_add_key(std::string_view name) -> std::size_t {
    const std::size_t existing = find_key(name);
    if (existing != npos) return existing;
    name_store_.emplace_back(name);
    value_store_.emplace_back();
    return name_store_.size() - 1;
}

auto fields::erase_order_for(std::size_t key) -> void {
    order_.erase(std::remove_if(order_.begin(), order_.end(), [key](const auto& slot) { return slot.first == key; }), order_.end());
}

auto fields::rebuild_cache() const -> void {
    entry_cache_.clear();
    entry_cache_.reserve(order_.size());
    for (const auto& [key, value_idx] : order_) {
        entry_cache_.push_back(entry{name_store_[key], value_store_[key][value_idx]});
    }
    cache_dirty_ = false;
}

}  // namespace httpserver::http

namespace httpserver::http {
bool operator==(const fields& a, const fields& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.order_.size(); ++i) {
        const auto& [ak, av] = a.order_[i];
        const auto& [bk, bv] = b.order_[i];
        if (!fields::iequals(a.name_store_[ak], b.name_store_[bk])) return false;
        if (a.value_store_[ak][av] != b.value_store_[bk][bv]) return false;
    }
    return true;
}
}  // namespace httpserver::http
