/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#if !defined(HTTPSERVER_COMPILATION)
#error "request_handler_frames.hpp is internal."
#endif
#ifndef SRC_HTTPSERVER_DETAIL_REQUEST_HANDLER_FRAMES_HPP_
#define SRC_HTTPSERVER_DETAIL_REQUEST_HANDLER_FRAMES_HPP_
#include <atomic>
#include <memory>
#include <utility>
#include <httpserver/concurrency/task.hpp>
namespace httpserver::detail::request_handler_frames {
// Serialized resumes retain executor affinity and invalidate queued frame
// witnesses on cancellation. Owners defer destruction until running() is false.
class route_executor final : public executor, public std::enable_shared_from_this<route_executor> {
 public:
    struct resume_state {
        std::size_t running = 0;
        std::atomic<bool> enabled{true};
        std::shared_ptr<handler> after_resume;
    };
    explicit route_executor(executor& owner, std::shared_ptr<executor> lifetime = {}) : owner_(owner), owner_lifetime_(std::move(lifetime)) {}
    void post(handler work) override;
    bool is_current() const noexcept override { return resumes_->enabled && current_executor() == this; }
    bool running() const { return resumes_->running != 0; }
    void disable() { resumes_->enabled = false; }
    void after_resume(handler callback) { resumes_->after_resume = callback ? std::make_shared<handler>(std::move(callback)) : nullptr; }

 private:
    executor& owner_;
    std::shared_ptr<executor> owner_lifetime_;
    std::shared_ptr<resume_state> resumes_ = std::make_shared<resume_state>();
};
// A consumer keeps final_suspend alive, so the stream owns every nested
// handler frame until reaping. Queued resumes carry the task's witness and
// become no-ops after destruction, including application-owned signals.
class route_task {
 public:
    ~route_task() { clear(); }
    void start(route_executor& owner, task<void> task);
    bool done() const { return frame_ && frame_.promise().state() == task_state::done; }
    void invalidate();
    void clear();

 private:
    std::coroutine_handle<task<void>::promise_type> frame_;
};
}  // namespace httpserver::detail::request_handler_frames
#endif  // SRC_HTTPSERVER_DETAIL_REQUEST_HANDLER_FRAMES_HPP_
