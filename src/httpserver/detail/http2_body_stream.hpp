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
#include <memory>
#include <utility>
#include <atomic>
#include <mutex>
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
    const http::outcome& failure() const noexcept override;
    void park(body_wait& wait) override;
    void unpark(body_wait& wait) override;
    body_push_result push(std::span<const std::byte> from) override;
    body_push_result push_end(const http::fields& trailers) override;
    void park(body_write_wait& wait) override;
    void unpark(body_write_wait& wait) override;
    std::vector<std::uint8_t> send_prefix(std::size_t n) const;
    void retire(std::size_t n);
    std::size_t queued() const { std::lock_guard lock(mutex_); return sent_.size; }
    std::uint64_t received_total() const { std::lock_guard lock(mutex_); return received_total_; }
    std::size_t unread() const { std::lock_guard lock(mutex_); return received_.size; }
    std::size_t receive_capacity() const { std::lock_guard lock(mutex_); return admitted_ ? policy_capacity_ : received_.bytes.size(); }
    bool admitted() const { std::lock_guard lock(mutex_); return admitted_; }
    bool receive_ended() const { std::lock_guard lock(mutex_); return receive_ended_; }
    bool finished() const { std::lock_guard lock(mutex_); return finished_; }
    const http::fields& sent_trailers() const { return sent_trailers_; }
    std::span<const std::byte> receive_prefix() const;
    void consume_received(std::size_t n);
    void discard_received();
    bool failed() const { std::lock_guard lock(mutex_); return failure_code_.load() != http::outcome_code::ok; }
    http2_window send_window, receive_window;
    std::atomic<std::uint64_t> consumed{0};
    // Installed before handler publication; notifications only schedule owner work.
    void observe_progress(executor::handler callback) { progress_ = std::move(callback); }

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
    class mutation {
     public:
        explicit mutation(http2_body_stream& body) : body_(body), lock_(body.mutex_) {}
        ~mutation() { lock_.unlock(); body_.publish(); }
     private:
        http2_body_stream& body_;
        std::unique_lock<std::mutex> lock_;
    };
    void publish();
    bool end_receive_locked(http::fields trailers = {});
    void fail_locked(http::outcome_code code = http::outcome_code::protocol_error);
    bool failed_locked() const { return failure_code_.load() != http::outcome_code::ok; }
    std::size_t receive_capacity_locked() const { return admitted_ ? policy_capacity_ : received_.bytes.size(); }
    mutable std::mutex mutex_;
    executor::handler progress_;
    server::resource_budget budget_;
    ring received_, sent_;
    server::reservation trailers_charge_;
    http::fields received_trailers_, sent_trailers_;
    std::atomic<http::outcome_code> failure_code_{http::outcome_code::ok};
    std::optional<std::uint64_t> receive_length_, send_length_;
    std::uint64_t received_total_ = 0, sent_total_ = 0;
    std::size_t policy_capacity_ = 0;
    std::shared_ptr<wait_node> reader_;
    std::shared_ptr<write_wait_node> writer_;
    bool admitted_ = false, receive_ended_ = false, eof_seen_ = false, finished_ = false, forbidden_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_BODY_STREAM_HPP_
