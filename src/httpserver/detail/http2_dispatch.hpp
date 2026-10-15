/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "http2_dispatch.hpp is internal"
#endif
#ifndef SRC_HTTPSERVER_DETAIL_HTTP2_DISPATCH_HPP_
#define SRC_HTTPSERVER_DETAIL_HTTP2_DISPATCH_HPP_
#include <array>
#include <memory>
#include <mutex>
#include <utility>
#include <httpserver/concurrency/task.hpp>
namespace httpserver::detail {
const void*& counted_drain_slot();
// A bounded serial affinity. One invocation per worker turn leaves network and
// sibling-stream work eligible. Overflow has its own terminal slot; retirement
// drops queued frame witnesses outside the scheduling lock.
class http2_dispatch final : public executor, public std::enable_shared_from_this<http2_dispatch> {
 public:
    explicit http2_dispatch(executor& workers, const void* drain = nullptr) : workers_(workers), drain_(drain) {}
    void on_overflow(handler terminal) { terminal_ = std::move(terminal); }
    void post(handler work) override;
    bool is_current() const noexcept override { return current_executor() == this; }
    void retire();

 private:
    void run_one();
    executor& workers_;
    const void* drain_;
    std::mutex mutex_;
    std::array<handler, 16> queue_;
    handler terminal_;
    std::size_t head_ = 0, count_ = 0;
    bool scheduled_ = false, retired_ = false;
};
}  // namespace httpserver::detail
#endif  // SRC_HTTPSERVER_DETAIL_HTTP2_DISPATCH_HPP_
