/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_body_stream.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_BODY_STREAM_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_BODY_STREAM_HPP_
#include <optional>
#include <vector>
#include <httpserver/body_reader.hpp>
#include <httpserver/response_writer.hpp>
#include <httpserver/server/budgets.hpp>
#include <httpserver/detail/http2_flow_control.hpp>
namespace httpserver::detail {
// Fixed-capacity rings avoid retaining consumed prefixes or allocating per read.
class http2_body_stream final : public body_source, public body_sink {
 public:
    explicit http2_body_stream(server::resource_budget budget) : budget_(budget) {}
    bool prepare_receive(std::size_t capacity, bool ended, std::optional<std::uint64_t> length);
    bool admit(std::size_t capacity);
    bool receive(std::span<const std::uint8_t> bytes, bool end);
    bool end_receive(http::fields trailers = {});
    bool prepare_send(std::size_t capacity, std::optional<std::uint64_t> length, bool forbidden);
    void fail(http::outcome_code code = http::outcome_code::protocol_error);
    body_pull_result pull(std::span<std::byte> into) override;
    const http::fields& trailers() const noexcept override {
        static const http::fields none;
        return eof_seen_ ? received_trailers_ : none;
    }
    const http::outcome& failure() const noexcept override { return failure_; }
    void park(body_wait& wait) override;
    void unpark(body_wait& wait) override;
    body_push_result push(std::span<const std::byte> from) override;
    body_push_result push_end(const http::fields& trailers) override;
    void park(body_write_wait& wait) override;
    void unpark(body_write_wait& wait) override;
    std::vector<std::uint8_t> send_prefix(std::size_t n) const;
    void retire(std::size_t n);
    std::size_t queued() const { return sent_.size; }
    std::size_t unread() const { return received_.size; }
    std::size_t receive_capacity() const { return admitted_ ? policy_capacity_ : received_.bytes.size(); }
    bool admitted() const { return admitted_; }
    bool receive_ended() const { return receive_ended_; }
    bool finished() const { return finished_; }
    const http::fields& sent_trailers() const { return sent_trailers_; }
    std::span<const std::byte> receive_prefix() const;
    void consume_received(std::size_t n);
    void discard_received() { if (received_.size) received_.consume(received_.size); }
    bool failed() const { return !failure_.ok(); }
    http2_window send_window, receive_window;
    std::uint64_t consumed = 0;

 private:
    struct ring {
        std::vector<std::byte> bytes;
        std::size_t head = 0, size = 0;
        server::reservation charge;
        bool prepare(server::resource_budget budget, server::resource kind, std::size_t capacity);
        void append(std::span<const std::byte> from);
        void consume(std::size_t n);
    };
    std::optional<std::size_t> trailer_size(const http::fields& trailers) const;
    bool admit_trailers(const http::fields& trailers);
    void wake_reader();
    void wake_writer();
    server::resource_budget budget_;
    ring received_, sent_;
    server::reservation trailers_charge_;
    http::fields received_trailers_, sent_trailers_;
    http::outcome failure_;
    std::optional<std::uint64_t> receive_length_, send_length_;
    std::uint64_t received_total_ = 0, sent_total_ = 0;
    std::size_t policy_capacity_ = 0;
    body_wait* reader_ = nullptr;
    body_write_wait* writer_ = nullptr;
    bool admitted_ = false, receive_ended_ = false, eof_seen_ = false, finished_ = false, forbidden_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_BODY_STREAM_HPP_
