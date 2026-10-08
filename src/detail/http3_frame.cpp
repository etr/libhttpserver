/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <algorithm>
#include <new>
#include <utility>
#include <vector>
#include <httpserver/detail/http3_frame.hpp>
namespace httpserver::detail {
namespace {
bool scalar_frame(std::uint64_t type) {
    constexpr std::array<std::uint64_t, 3> types{3, 7, 13};
    return std::find(types.begin(), types.end(), type) != types.end();
}
}  // namespace
http3_frame_parser::http3_frame_parser(quic_storage_lease data, quic_storage_lease critical, http3_limits limits, std::uint64_t offset)
    : data_(std::move(data)), critical_(std::move(critical)), limits_(limits), offset_(offset), frame_begin_(offset) {}
std::optional<http3_error> http3_frame_parser::fail(std::uint64_t code, std::string_view diagnostic) {
    if (!error_) error_ = h3_error(code, diagnostic);
    return error_;
}
http3_feed_result http3_frame_parser::result(std::size_t consumed) const {
    auto progress = http3_progress::input;
    if (phase_ == phase::admission) progress = http3_progress::header;
    if (ready_) progress = http3_progress::event;
    if (error_) progress = http3_progress::failed;
    return {progress, consumed, error_};
}
bool http3_frame_parser::read_integer(std::span<const std::byte>* input, std::uint64_t* value) {
    while (!input->empty()) {
        integer_[integer_size_++] = (*input)[0];
        *input = input->subspan(1); ++offset_;
        const auto width = std::size_t{1} << (std::to_integer<unsigned>(integer_[0]) >> 6);
        if (integer_size_ != width) continue;
        *value = decode_quic_varint(std::span(integer_).first(width)).value;
        integer_size_ = 0;
        return true;
    }
    return false;
}
std::optional<http3_error> http3_frame_parser::reserve_payload() {
    if (!length_) return {};
    auto& storage = type_ == 1 ? data_ : critical_;
    try {
        if (!storage.budget.reserve(server::resource::quic_reassembly_bytes, static_cast<std::size_t>(length_), payload_charge_).ok())
            return fail(0x107, "Payload storage exhausted");
        payload_.reserve(static_cast<std::size_t>(length_));
    } catch (const std::bad_alloc&) {
        payload_charge_.release(); return fail(0x102, "Payload allocation failed");
    } catch (const std::length_error&) {
        payload_charge_.release(); return fail(0x102, "Payload allocation size invalid");
    }
    return {};
}
std::optional<http3_error> http3_frame_parser::accept_header() {
    if (error_) return error_;
    if (phase_ != phase::admission) return fail(0x102, "Header admission out of order");
    buffered_ = type_ == 1 || type_ == 4;
    if (validate_header()) return error_;
    if (buffered_ && reserve_payload()) return error_;
    remaining_ = length_; payload_begin_ = offset_; phase_ = phase::payload;
    return {};
}
std::optional<http3_error> http3_frame_parser::validate_header() {
    const bool scalar = scalar_frame(type_);
    const auto cap = type_ == 1 ? limits_.headers.max_compressed_bytes : limits_.settings_bytes;
    if (buffered_ && length_ > cap) return fail(0x107, "Frame payload exceeds bound");
    if (scalar && length_ > scalar_.size()) return fail(0x106, "Scalar frame payload exceeds bound");
    if (type_ == 0 && !limits_.data_chunk) return fail(0x102, "Zero DATA chunk limit");
    return {};
}
void http3_frame_parser::complete_payload(std::span<const std::byte> payload, std::uint64_t begin) {
    event_ = {type_, frame_begin_, begin, offset_, payload, {}, remaining_ == 0};
    ready_ = true;
}
http3_feed_result http3_frame_parser::feed(std::span<const std::byte> input, std::uint64_t offset) {
    if (error_ || ready_ || phase_ == phase::admission) return result(0);
    if (!h3_ordered_extent(offset, input.size(), offset_)) {
        fail(0x102, "Invalid ordered transport extent"); return result(0);
    }
    const auto size = input.size();
    read_header(&input);
    if (phase_ != phase::payload) return result(size - input.size());
    return result(size - input.size() + read_payload(input));
}
void http3_frame_parser::read_header(std::span<const std::byte>* input) {
    if (phase_ == phase::type && read_integer(input, &type_)) phase_ = phase::length;
    if (phase_ == phase::length && read_integer(input, &length_)) phase_ = phase::admission;
}
std::size_t http3_frame_parser::read_payload(std::span<const std::byte> input) {
    auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, input.size()));
    if (type_ == 0) count = std::min(count, limits_.data_chunk);
    const auto begin = offset_;
    const bool scalar = scalar_frame(type_);
    if (buffered_) payload_.insert(payload_.end(), input.begin(), input.begin() + count);
    if (scalar) std::copy_n(input.begin(), count, scalar_.begin() + static_cast<std::size_t>(length_ - remaining_));
    remaining_ -= count; offset_ += count;
    if (type_ == 0) {
        if (count || !remaining_) complete_payload(input.first(count), begin);
    } else if (!remaining_) {
        complete_payload(scalar ? std::span(scalar_).first(static_cast<std::size_t>(length_)) : std::span<const std::byte>(payload_), payload_begin_);
    }
    return count;
}
void http3_frame_parser::release_event() {
    if (!ready_) return;
    ready_ = false; event_ = {};
    if (remaining_) return;
    // Free capacity before releasing its lease; clear() alone retains memory.
    std::vector<std::byte>().swap(payload_); payload_charge_.release();
    phase_ = phase::type; frame_begin_ = offset_; buffered_ = false;
}
std::optional<http3_error> http3_frame_parser::finish() {
    if (error_) return error_;
    if (ready_ && remaining_ == 0) return {};
    if (phase_ != phase::type || integer_size_) return fail(0x106, "Truncated HTTP/3 frame at FIN");
    return {};
}
}  // namespace httpserver::detail
