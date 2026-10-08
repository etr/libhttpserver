/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http3_body_stream.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP3_BODY_STREAM_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP3_BODY_STREAM_HPP_
#include <functional>
#include <utility>
#include <vector>
#include <httpserver/detail/http2_body_stream.hpp>
namespace httpserver::detail {
// Reuse the bounded semantic rings, not HTTP/2 transport/window accounting.
// QUIC receipts are advanced synchronously only by a successful application pull.
class http3_body_stream final : public body_source, public body_sink {
 public:
    explicit http3_body_stream(server::resource_budget budget) : rings_(budget) {}
    bool prepare_receive(std::size_t capacity, std::optional<std::uint64_t> length) { return rings_.prepare_receive(capacity, false, length); }
    bool admit(std::size_t capacity) { return rings_.admit(capacity); }
    std::size_t room() const { return rings_.receive_capacity() - rings_.unread(); }
    bool receive(std::span<const std::byte> bytes);
    bool end_receive(http::fields trailers) { return rings_.end_receive(std::move(trailers)); }
    bool receive_ended() const { return rings_.receive_ended(); }
    void discard() { rings_.discard_received(); }
    void fail(http::outcome_code code) { rings_.fail(code); }
    void on_consumed(std::function<void(std::size_t)> callback) { consumed_ = std::move(callback); }
    body_pull_result pull(std::span<std::byte> into) override;
    const http::fields& trailers() const noexcept override { return rings_.trailers(); }
    const http::outcome& failure() const noexcept override { return rings_.failure(); }
    void park(body_wait& wait) override { rings_.park(wait); }
    void unpark(body_wait& wait) override { rings_.unpark(wait); }
    bool prepare_send(std::size_t capacity, std::optional<std::uint64_t> length, bool forbidden) { return rings_.prepare_send(capacity, length, forbidden); }
    body_push_result push(std::span<const std::byte> bytes) override { return rings_.push(bytes); }
    body_push_result push_end(const http::fields& trailers) override { return rings_.push_end(trailers); }
    void park(body_write_wait& wait) override { rings_.park(wait); }
    void unpark(body_write_wait& wait) override { rings_.unpark(wait); }
    std::vector<std::byte> send_prefix(std::size_t size) const;
    void retire(std::size_t size) { rings_.retire(size); }
    std::size_t queued() const { return rings_.queued(); }
    bool finished() const { return rings_.finished(); }
    const http::fields& sent_trailers() const { return rings_.sent_trailers(); }

 private:
    http2_body_stream rings_;
    std::function<void(std::size_t)> consumed_;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP3_BODY_STREAM_HPP_
