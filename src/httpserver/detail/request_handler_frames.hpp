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
    explicit route_executor(executor& owner) : owner_(owner) {}
    void post(handler work) override {
        owner_.post([affinity = shared_from_this(), resume = resumes_, work = std::move(work)]() mutable {
            if (!resume->enabled) return;
            struct guard {
                std::shared_ptr<resume_state> resume;
                executor* previous;
                std::shared_ptr<void> previous_lifetime;
                ~guard() {
                    current_executor_slot() = previous;
                    current_task_executor_lifetime() = std::move(previous_lifetime);
                    if (--resume->running == 0 && resume->after_resume) {
                        auto callback = resume->after_resume;
                        (*callback)();
                    }
                }
            };
            ++resume->running;
            guard lifetime{resume, current_executor_slot(), std::move(current_task_executor_lifetime())};
            current_task_executor_lifetime() = affinity;
            current_executor_slot() = affinity.get();
            work();
        });
    }
    bool is_current() const noexcept override { return resumes_->enabled && current_executor() == this; }
    bool running() const { return resumes_->running != 0; }
    void disable() { resumes_->enabled = false; }
    void after_resume(handler callback) { resumes_->after_resume = callback ? std::make_shared<handler>(std::move(callback)) : nullptr; }

 private:
    executor& owner_;
    std::shared_ptr<resume_state> resumes_ = std::make_shared<resume_state>();
};
// A consumer keeps final_suspend alive, so the stream owns every nested
// handler frame until reaping. Queued resumes carry the task's witness and
// become no-ops after destruction, including application-owned signals.
class route_task {
 public:
    ~route_task() { clear(); }
    void start(route_executor& owner, task<void> task) {
        frame_ = task.release();
        auto& promise = frame_.promise();
        promise.try_consume();
        promise.set_affinity(&owner, owner.shared_from_this());
        promise.register_consumer(std::noop_coroutine(), &owner, {});
        owner.post([witness = promise.frame_witness_ptr(), frame = frame_] { guarded_resume(witness, frame); });
    }
    bool done() const { return frame_ && frame_.promise().state() == task_state::done; }
    void invalidate() {
        if (frame_) frame_.promise().invalidate();
    }
    void clear() {
        if (!frame_) return;
        frame_.promise().invalidate();
        frame_.destroy();
        frame_ = {};
    }

 private:
    std::coroutine_handle<task<void>::promise_type> frame_;
};
}  // namespace httpserver::detail::request_handler_frames
#endif  // SRC_HTTPSERVER_DETAIL_REQUEST_HANDLER_FRAMES_HPP_
