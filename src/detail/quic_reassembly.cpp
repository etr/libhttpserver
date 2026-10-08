/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <httpserver/detail/quic_reassembly.hpp>
namespace httpserver::detail {
struct quic_reassembly::range {
    std::uint64_t begin = 0, end = 0;
    std::size_t skip = 0, capacity = 0;
    server::reservation charge;
    std::unique_ptr<std::byte[]> bytes;
};
struct quic_reassembly::insertion {
    std::size_t first = 0, last = 0, unique = 0, replaced_storage = 0;
    std::uint64_t begin = 0, end = 0;
};
void quic_reassembly::range_deleter::operator()(range* pointer) const noexcept {
    std::destroy_n(pointer, count);
    std::allocator<range>{}.deallocate(pointer, count);
}
quic_reassembly::quic_reassembly(quic_stream_limits limits, server::resource_budget budget)
    : limits_(limits), budget_(std::move(budget)) {
    storage_capacity(limits);
}
quic_reassembly::~quic_reassembly() = default;
quic_stream_result quic_reassembly::insert(std::uint64_t offset, std::span<const std::byte> data) {
    if (offset > k_quic_max_integer || data.size() > k_quic_max_integer - offset)
        return {quic_stream_code::frame_encoding_error};
    const auto end = offset + data.size();
    if (end <= consumed_) return {};
    if (end - consumed_ > limits_.max_offset_span) return {quic_stream_code::gap_limit_exceeded};
    if (data.empty()) return {};
    if (offset < consumed_) {
        data = data.subspan(static_cast<std::size_t>(consumed_ - offset));
        offset = consumed_;
    }
    return insert_unread(offset, data);
}
quic_stream_result quic_reassembly::prepare_insert(std::uint64_t offset, std::span<const std::byte> data, insertion& plan) const {
    const auto end = offset + data.size();
    while (plan.first < count_ && ranges_[plan.first].end < offset) ++plan.first;
    plan.last = plan.first;
    plan.unique = data.size();
    plan.begin = offset;
    plan.end = end;
    while (plan.last < count_ && ranges_[plan.last].begin <= plan.end) {
        const auto& old = ranges_[plan.last];
        const auto overlap_begin = std::max(offset, old.begin), overlap_end = std::min(end, old.end);
        if (overlap_begin < overlap_end) {
            const auto length = static_cast<std::size_t>(overlap_end - overlap_begin);
            if (!std::equal(data.begin() + (overlap_begin - offset), data.begin() + (overlap_end - offset),
                            old.bytes.get() + old.skip + (overlap_begin - old.begin)))
                return {quic_stream_code::protocol_violation};
            plan.unique -= length;
        }
        plan.begin = std::min(plan.begin, old.begin);
        plan.end = std::max(plan.end, old.end);
        plan.replaced_storage += old.capacity;
        ++plan.last;
    }
    return {};
}
quic_stream_result quic_reassembly::insert_unread(std::uint64_t offset, std::span<const std::byte> data) {
    insertion plan;
    const auto checked = prepare_insert(offset, data, plan);
    if (!checked) return checked;
    if (plan.unique == 0) return {};
    if (plan.unique > limits_.max_buffered_bytes - buffered_) return {quic_stream_code::byte_limit_exceeded};
    if (count_ - (plan.last - plan.first) >= limits_.max_ranges) return {quic_stream_code::gap_limit_exceeded};
    const auto length = static_cast<std::size_t>(plan.end - plan.begin);
    // Partial reads retain their original allocation. Bound both retained
    // capacity and old-plus-staged payload, rather than only unread bytes.
    if (length > limits_.max_buffered_bytes - (storage_ - plan.replaced_storage))
        return {quic_stream_code::byte_limit_exceeded};
    return stage_insert(offset, data, plan);
}
quic_stream_result quic_reassembly::stage_insert(std::uint64_t offset, std::span<const std::byte> data, const insertion& plan) {
    const auto length = static_cast<std::size_t>(plan.end - plan.begin);
    try {
        server::reservation metadata;
        std::unique_ptr<range[], range_deleter> descriptors{nullptr, range_deleter{limits_.max_ranges}};
        if (!ranges_) {
            if (!budget_.reserve(server::resource::quic_reassembly_bytes, limits_.max_ranges * sizeof(range), metadata).ok())
                return {quic_stream_code::no_memory};
            // allocator<T> requests exactly n*sizeof(T), avoiding the hidden
            // new[] cookie of descriptors with nontrivial destructors.
            auto* allocated = std::allocator<range>{}.allocate(limits_.max_ranges);
            std::uninitialized_value_construct_n(allocated, limits_.max_ranges);
            descriptors.reset(allocated);
        }
        range replacement;
        if (!budget_.reserve(server::resource::quic_reassembly_bytes, length, replacement.charge).ok())
            return {quic_stream_code::no_memory};
        replacement.bytes = std::make_unique<std::byte[]>(length);
        replacement.begin = plan.begin;
        replacement.end = plan.end;
        replacement.capacity = length;
        for (auto i = plan.first; i < plan.last; ++i) {
            const auto& old = ranges_[i];
            std::copy_n(old.bytes.get() + old.skip, static_cast<std::size_t>(old.end - old.begin), replacement.bytes.get() + (old.begin - plan.begin));
        }
        std::copy(data.begin(), data.end(), replacement.bytes.get() + (offset - plan.begin));
        if (descriptors) {
            ranges_ = std::move(descriptors);
            metadata_ = std::move(metadata);
        }
        commit_insert(std::move(replacement), plan);
        return {};
    } catch (const std::bad_alloc&) {
        return {quic_stream_code::no_memory};
    }
}
void quic_reassembly::commit_insert(range replacement, const insertion& plan) {
    // After staging there are no throwing operations or peer checks.
    for (auto i = plan.first; i < plan.last; ++i) ranges_[i] = range{};
    if (plan.first == plan.last) {
        for (auto i = count_; i > plan.first; --i) ranges_[i] = std::move(ranges_[i - 1]);
    } else if (plan.last > plan.first + 1) {
        for (auto i = plan.last; i < count_; ++i) ranges_[plan.first + 1 + i - plan.last] = std::move(ranges_[i]);
    }
    storage_ = storage_ - plan.replaced_storage + replacement.capacity;
    ranges_[plan.first] = std::move(replacement);
    count_ = count_ - (plan.last - plan.first) + 1;
    buffered_ += plan.unique;
}
std::size_t quic_reassembly::contiguous_bytes() const {
    return count_ && ranges_[0].begin == consumed_ ? static_cast<std::size_t>(ranges_[0].end - consumed_) : 0;
}
std::size_t quic_reassembly::read(std::span<std::byte> output) {
    const auto length = std::min(output.size(), contiguous_bytes());
    if (!length) return 0;
    auto& front = ranges_[0];
    std::copy_n(front.bytes.get() + front.skip, length, output.begin());
    consumed_ += length;
    buffered_ -= length;
    front.begin += length;
    front.skip += length;
    if (front.begin == front.end) {
        storage_ -= front.capacity;
        front = range{};
        for (std::size_t i = 1; i < count_; ++i) ranges_[i - 1] = std::move(ranges_[i]);
        --count_;
    }
    return length;
}
void quic_reassembly::clear() {
    ranges_.reset();
    metadata_.release();
    count_ = buffered_ = storage_ = 0;
}
std::size_t quic_reassembly::retained_storage() const { return storage_ + metadata_.units(); }

quic_reassembly::quic_reassembly(quic_stream_limits limits, quic_storage_lease storage)
    : quic_reassembly(limits, storage.budget) { storage_owner_ = std::move(storage); }
std::size_t quic_reassembly::storage_capacity(quic_stream_limits limits) {
    const auto maximum = std::numeric_limits<std::size_t>::max();
    if (limits.max_ranges > maximum / sizeof(range) || limits.max_offset_span > k_quic_max_integer ||
        limits.max_buffered_bytes > (maximum - limits.max_ranges * sizeof(range)) / 2)
        throw std::invalid_argument("QUIC reassembly limits overflow storage accounting");
    return limits.max_ranges * sizeof(range) + 2 * limits.max_buffered_bytes;
}
}  // namespace httpserver::detail
