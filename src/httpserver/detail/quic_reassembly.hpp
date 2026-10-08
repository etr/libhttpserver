/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "quic_reassembly.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_QUIC_REASSEMBLY_HPP_
#define SRC_HTTPSERVER_DETAIL_QUIC_REASSEMBLY_HPP_
#include <memory>
#include <span>
#include <httpserver/detail/quic_codec.hpp>
#include <httpserver/server/budgets.hpp>
namespace httpserver::detail {
struct quic_stream_limits {
    std::size_t max_buffered_bytes = 65536;
    std::size_t max_ranges = 64;
    std::uint64_t max_offset_span = 1048576;
};
enum class quic_stream_code {
    ok, stream_state_error, final_size_error, frame_encoding_error,
    protocol_violation, byte_limit_exceeded, gap_limit_exceeded, no_memory
};
struct quic_stream_result {
    quic_stream_code code = quic_stream_code::ok;
    explicit operator bool() const noexcept { return code == quic_stream_code::ok; }
};
// Connection-owner serialized. Accepted input is copied; reads consume only
// contiguous bytes. No offset-sized allocation or retained consumed history.
class quic_reassembly final {
 public:
    quic_reassembly(quic_stream_limits limits, server::resource_budget budget);
    ~quic_reassembly();
    quic_reassembly(const quic_reassembly&) = delete;
    quic_reassembly& operator=(const quic_reassembly&) = delete;
    quic_stream_result insert(std::uint64_t offset, std::span<const std::byte> data);
    std::size_t read(std::span<std::byte> output);
    void clear();
    std::uint64_t consumed() const { return consumed_; }
    std::size_t buffered_bytes() const { return buffered_; }
    std::size_t ranges() const { return count_; }
    std::size_t retained_storage() const;
    std::size_t contiguous_bytes() const;

 private:
    struct range;
    struct insertion;
    quic_stream_result insert_unread(std::uint64_t offset, std::span<const std::byte> data);
    quic_stream_result prepare_insert(std::uint64_t offset, std::span<const std::byte> data, insertion& plan) const;
    quic_stream_result stage_insert(std::uint64_t offset, std::span<const std::byte> data, const insertion& plan);
    void commit_insert(range replacement, const insertion& plan);
    struct range_deleter {
        std::size_t count;
        void operator()(range* pointer) const noexcept;
    };
    quic_stream_limits limits_;
    server::resource_budget budget_;
    server::reservation metadata_;
    std::unique_ptr<range[], range_deleter> ranges_{nullptr, range_deleter{0}};
    std::size_t count_ = 0, buffered_ = 0, storage_ = 0;
    std::uint64_t consumed_ = 0;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_QUIC_REASSEMBLY_HPP_
