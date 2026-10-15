/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/detail/request_handler_frames.hpp>
#include <memory>
#include <utility>

namespace httpserver::detail::request_handler_frames {

auto route_executor::post(handler work) -> void {
    if (owner_lifetime_ && !resumes_->enabled.load()) return;
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

auto route_task::start(route_executor& owner, task<void> task) -> void {
    frame_ = task.release();
    auto& promise = frame_.promise();
    promise.try_consume();
    promise.set_affinity(&owner, owner.shared_from_this());
    promise.register_consumer(std::noop_coroutine(), &owner, {});
    owner.post([witness = promise.frame_witness_ptr(), frame = frame_] { guarded_resume(witness, frame); });
}

auto route_task::invalidate() -> void {
    if (frame_) frame_.promise().invalidate();
}

auto route_task::clear() -> void {
    if (!frame_) return;
    frame_.promise().invalidate();
    frame_.destroy();
    frame_ = {};
}

}  // namespace httpserver::detail::request_handler_frames
