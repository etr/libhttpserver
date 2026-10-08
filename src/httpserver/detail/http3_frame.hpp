/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http3_frame.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP3_FRAME_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP3_FRAME_HPP_
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <httpserver/detail/quic_storage.hpp>
#include <httpserver/detail/quic_varint.hpp>
#include <httpserver/detail/qpack_field_section.hpp>
namespace httpserver::detail {
enum class http3_role { pending, request, control, qpack_encoder, qpack_decoder, discard };
enum class http3_error_scope { connection };
struct http3_error {
    http3_error_scope scope = http3_error_scope::connection;
    std::uint64_t wire_code = 0;
    std::uint64_t stream_id = 0;
    http::outcome_code outcome = http::outcome_code::protocol_error;
    std::string_view diagnostic;
};
inline http3_error h3_error(std::uint64_t code, std::string_view diagnostic, std::uint64_t id = 0) {
    return {http3_error_scope::connection, code, id, code == 0x107 ? http::outcome_code::limit_exceeded : http::outcome_code::protocol_error, diagnostic};
}
inline bool h3_ordered_extent(std::uint64_t offset, std::size_t size, std::uint64_t expected) {
    return offset == expected && offset <= k_quic_max_integer && size <= k_quic_max_integer - offset;
}
enum class http3_progress { input, header, event, yield, failed };
struct http3_feed_result {
    http3_progress progress = http3_progress::input;
    std::size_t consumed = 0;
    std::optional<http3_error> error;
};
struct http3_limits {
    qpack_section_limits headers{65536, 65536, 256};
    std::size_t settings_bytes = 4096;
    std::size_t settings_identifiers = 64;  // Hard ceiling: 64.
    std::size_t data_chunk = 16384;
    std::size_t frames_per_turn = 64;
    std::size_t stream_records = 128;  // Includes terminal facts, preventing recreation.
};
struct http3_event {
    std::uint64_t type = 0;
    std::uint64_t frame_begin = 0, payload_begin = 0, payload_end = 0;
    std::span<const std::byte> payload;
    std::span<const qpack_field> fields;
    bool last = true;
};
// One serialized stream. DATA borrows immutable caller input until release;
// other known payloads are owned. Header admission precedes any payload growth.
// Release controls lifetime only; callers explicitly account transport receipts.
class http3_frame_parser final {
 public:
    http3_frame_parser(quic_storage_lease data, quic_storage_lease critical, http3_limits limits = {}, std::uint64_t offset = 0);
    http3_feed_result feed(std::span<const std::byte> input, std::uint64_t offset);
    std::optional<http3_error> accept_header();
    const http3_event* event() const { return ready_ ? &event_ : nullptr; }
    void release_event();
    std::optional<http3_error> finish();
    std::uint64_t type() const { return type_; }
    std::uint64_t length() const { return length_; }
    std::uint64_t offset() const { return offset_; }

 private:
    enum class phase { type, length, admission, payload };
    void read_header(std::span<const std::byte>* input);
    bool read_integer(std::span<const std::byte>* input, std::uint64_t* value);
    std::optional<http3_error> validate_header();
    std::optional<http3_error> reserve_payload();
    std::size_t read_payload(std::span<const std::byte> input);
    void complete_payload(std::span<const std::byte> payload, std::uint64_t begin);
    std::optional<http3_error> fail(std::uint64_t code, std::string_view diagnostic);
    http3_feed_result result(std::size_t consumed) const;
    quic_storage_lease data_, critical_;
    http3_limits limits_;
    server::reservation payload_charge_;
    std::vector<std::byte> payload_;
    std::array<std::byte, 8> scalar_{};
    std::array<std::byte, 8> integer_{};
    std::size_t integer_size_ = 0;
    phase phase_ = phase::type;
    std::uint64_t type_ = 0, length_ = 0, remaining_ = 0, offset_ = 0, frame_begin_ = 0, payload_begin_ = 0;
    http3_event event_;
    bool ready_ = false, buffered_ = false;
    std::optional<http3_error> error_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP3_FRAME_HPP_
