/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include <httpserver/detail/qpack_dynamic_table.hpp>

namespace httpserver::detail {

auto qpack_reference_hold::operator=(qpack_reference_hold&& other) noexcept -> qpack_reference_hold& {
    if (this != &other) {
        release();
        entries_.swap(other.entries_);
    }
    return *this;
}

auto qpack_reference_hold::release() noexcept -> void {
    for (auto* entry : entries_) --entry->references;
    entries_.clear();
}

auto qpack_dynamic_table::initialize(const server::resource_budget& budget, std::size_t maximum) -> qpack_status {
    if (initialized_ || maximum > qpack_integer_maximum) return {qpack_state::invalid_argument};
    try {
        server::reservation charge;
        if (maximum != 0 && !budget.reserve(server::resource::qpack_table_bytes, maximum, charge).ok()) {
            return {qpack_state::limit_exceeded};
        }
        charge_ = std::move(charge);
        maximum_ = maximum;
        initialized_ = true;
        return {};
    } catch (const std::bad_alloc&) {
        return {qpack_state::limit_exceeded};
    } catch (const std::length_error&) {
        return {qpack_state::limit_exceeded};
    }
}

auto qpack_dynamic_table::clear() noexcept -> void {
    entries_.clear();
    charge_.release();
    bytes_ = capacity_ = maximum_ = 0;
    inserts_ = received_ = 0;
    initialized_ = false;
}

auto qpack_dynamic_table::lookup(std::uint64_t absolute) const noexcept -> const qpack_dynamic_entry* {
    if (entries_.empty() || absolute < entries_.front().absolute || absolute >= inserts_) return nullptr;
    for (const auto& entry : entries_)
        if (entry.absolute == absolute) return &entry;
    return nullptr;
}

auto qpack_dynamic_table::find(std::string_view name, std::string_view value) const noexcept -> std::optional<std::uint64_t> {
    for (auto i = entries_.rbegin(); i != entries_.rend(); ++i) {
        if (i->name == name && i->value == value) return i->absolute;
    }
    return std::nullopt;
}

auto qpack_dynamic_table::find_name(std::string_view name) const noexcept -> std::optional<std::uint64_t> {
    for (auto i = entries_.rbegin(); i != entries_.rend(); ++i) {
        if (i->name == name) return i->absolute;
    }
    return std::nullopt;
}

auto qpack_dynamic_table::set_capacity(std::size_t capacity) noexcept -> qpack_status {
    if (!initialized_) return {qpack_state::invalid_argument};
    if (capacity > maximum_) return {qpack_state::malformed};
    const auto status = can_evict_to(capacity);
    if (!status.ok()) return status;
    evict_to(capacity);
    capacity_ = capacity;
    return {};
}

auto qpack_dynamic_table::insert(std::string_view name, std::string_view value) -> qpack_status {
    try {
        return insert_allocated(name, value);
    } catch (const std::bad_alloc&) {
        return {qpack_state::limit_exceeded};
    } catch (const std::length_error&) {
        return {qpack_state::limit_exceeded};
    }
}

auto qpack_dynamic_table::insert_allocated(std::string_view name, std::string_view value) -> qpack_status {
    if (!initialized_) return {qpack_state::invalid_argument};
    if (name.size() > SIZE_MAX - 32 || value.size() > SIZE_MAX - 32 - name.size() || inserts_ == qpack_integer_maximum) {
        return {qpack_state::limit_exceeded};
    }
    const auto size = name.size() + value.size() + 32;
    if (size > capacity_) return {qpack_state::malformed};
    const auto status = can_evict_to(capacity_ - size);
    if (!status.ok()) return status;
    // A prepared node owns borrowed input before its source is evicted;
    // list splice and FIFO removal cannot throw during commit.
    std::list<qpack_dynamic_entry> prepared;
    prepared.push_back({std::string(name), std::string(value), inserts_, size, 0});
    evict_to(capacity_ - size);
    entries_.splice(entries_.end(), prepared);
    bytes_ += size;
    ++inserts_;
    return {};
}

auto qpack_dynamic_table::duplicate(std::uint64_t relative) -> qpack_status {
    const auto* source = encoder_relative(relative);
    return source ? insert(source->name, source->value) : qpack_status{qpack_state::malformed};
}

auto qpack_dynamic_table::advance_known_received(std::uint64_t count) noexcept -> qpack_status {
    if (!initialized_ || role_ != qpack_table_role::encoder) return {qpack_state::invalid_argument};
    if (count < received_ || count > inserts_) return {qpack_state::malformed};
    received_ = count;
    return {};
}

auto qpack_dynamic_table::increment_known_received(std::uint64_t count) noexcept -> qpack_status {
    if (count == 0 || count > inserts_ - received_) return {qpack_state::malformed};
    return advance_known_received(received_ + count);
}

auto qpack_dynamic_table::acquire(std::span<const std::uint64_t> indexes, std::size_t maximum, qpack_reference_hold& out) -> qpack_status {
    if (!initialized_ || role_ != qpack_table_role::encoder || out.size() != 0) return {qpack_state::invalid_argument};
    if (indexes.size() > maximum) return {qpack_state::limit_exceeded};
    try {
        std::vector<std::uint64_t> unique(indexes.begin(), indexes.end());
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        qpack_reference_hold prepared;
        prepared.entries_.reserve(unique.size());
        for (auto index : unique) {
            auto* entry = const_cast<qpack_dynamic_entry*>(lookup(index));
            if (!entry) return {qpack_state::malformed};
            if (entry->references == SIZE_MAX) return {qpack_state::limit_exceeded};
        }
        // No pin is acquired until every index and allocation is admitted.
        for (auto index : unique) {
            auto* entry = const_cast<qpack_dynamic_entry*>(lookup(index));
            prepared.entries_.push_back(entry);
            ++entry->references;
        }
        out = std::move(prepared);
        return {};
    } catch (const std::bad_alloc&) {
        return {qpack_state::limit_exceeded};
    } catch (const std::length_error&) {
        return {qpack_state::limit_exceeded};
    }
}

auto qpack_dynamic_table::can_evict_to(std::size_t limit) const noexcept -> qpack_status {
    auto retained = bytes_;
    for (const auto& entry : entries_) {
        if (retained <= limit) break;
        if (role_ == qpack_table_role::encoder && (entry.absolute >= received_ || entry.references != 0)) {
            return {qpack_state::eviction_prohibited};
        }
        retained -= entry.bytes;
    }
    return {};
}

auto qpack_dynamic_table::evict_to(std::size_t limit) noexcept -> void {
    while (bytes_ > limit) {
        bytes_ -= entries_.front().bytes;
        entries_.pop_front();
    }
}

}  // namespace httpserver::detail
