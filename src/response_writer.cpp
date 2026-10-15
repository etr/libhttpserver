/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/response_writer.hpp>
#include <utility>

namespace httpserver::detail {

auto write_wait_node::claim() noexcept -> bool {
    bool expected = false;
    return delivered.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

auto write_wait_node::resume_posted() noexcept -> void {
    if (target != nullptr) {
        try {
            target->post([witness = witness, frame = awaiting] {
                if (witness) {
                    guarded_resume(witness, frame);
                } else {
                    frame.resume();
                }
            });
        } catch (...) {
        }
        return;
    }
    if (witness) {
        guarded_resume(witness, awaiting);
    } else {
        awaiting.resume();
    }
}

body_write_wait::body_write_wait(body_sink* sink, stop_token cancel) : sink_(sink), node_(std::make_shared<write_wait_node>()), cancel_(std::move(cancel)) {}

auto body_write_wait::bind_frame(task_frame_base* frame) noexcept -> body_write_wait& {
    frame_ = frame;
    return *this;
}

auto body_write_wait::await_suspend(std::coroutine_handle<> awaiting) -> std::coroutine_handle<> {
    node_->awaiting = awaiting;
    node_->witness = frame_ ? frame_->frame_witness_ptr() : nullptr;
    node_->target = frame_ ? frame_->frame_executor() : current_executor();
    sink_->park(*this);
    // Seam contract: completion is posted to this executor, never
    // resumed inline on the parking (or completing) thread.
    return std::noop_coroutine();
}

auto body_write_wait::complete(body_room room) noexcept -> void {
    if (!node_->claim()) return;
    node_->result = room;
    node_->resume_posted();
}

body_write_wait::~body_write_wait() {
    if (!node_->delivered.load(std::memory_order_acquire)) {
        sink_->unpark(*this);
    }
}

}  // namespace httpserver::detail

namespace httpserver {

auto response_writer::write(std::span<const std::byte> data) -> task<body_write> {
    const http::outcome gate = enter(data.empty());
    if (!gate.ok()) {
        co_return body_write{gate, 0};
    }
    pending_ = true;
    std::size_t total = 0;
    while (total < data.size()) {
        const detail::body_push_result pushed = sink_->push(data.subspan(total));
        if (pushed.kind == detail::body_push::accepted) {
            // A zero-byte accepted push is engine misuse: count
            // nothing and re-push (never surface progress).
            total += pushed.copied;
            continue;
        }
        if (pushed.kind == detail::body_push::failed) {
            fail_sticky(sink_->failure());
            pending_ = false;
            co_return body_write{failure_, total};
        }
        // Queue at capacity: park until the first of queue room,
        // engine failure, or exchange disconnect. This suspension
        // IS the backpressure (PRD-V3N-REQ-027).
        detail::body_write_wait wait(sink_, cancel_token_);
        if (co_await wait == detail::body_room::cancelled) {
            pending_ = false;
            co_return body_write{http::outcome(http::outcome_code::cancelled,
                                               "response_writer: write cancelled by "
                                               "disconnect"),
                                 total};
        }
    }
    pending_ = false;
    co_return body_write{http::outcome::okay(), total};
}

auto response_writer::finish(http::fields trailers) -> task<body_finish> {
    const http::outcome gate = enter(false);
    if (!gate.ok()) {
        co_return body_finish{gate};
    }
    pending_ = true;
    for (;;) {
        const detail::body_push_result pushed = sink_->push_end(trailers);
        if (pushed.kind == detail::body_push::accepted) {
            finished_ = true;
            pending_ = false;
            close();
            co_return body_finish{http::outcome::okay()};
        }
        if (pushed.kind == detail::body_push::failed) {
            fail_sticky(sink_->failure());
            pending_ = false;
            co_return body_finish{failure_};
        }
        detail::body_write_wait wait(sink_, cancel_token_);
        if (co_await wait == detail::body_room::cancelled) {
            pending_ = false;
            co_return body_finish{http::outcome(http::outcome_code::cancelled,
                                                "response_writer: finish cancelled by "
                                                "disconnect")};
        }
    }
}

auto response_writer::activate(detail::body_sink* sink) noexcept -> void {
    sink_ = sink;
    started_ = true;
}

auto response_writer::enter(bool empty_chunk) const -> http::outcome {
    if (pending_) {
        return http::outcome(http::outcome_code::invalid_state,
                             "response_writer: a write is already "
                             "outstanding");
    }
    if (cancel_token_.stop_requested()) {
        return http::outcome(http::outcome_code::connection_closed, disconnect_reason_.message());
    }
    if (closed_) {
        return http::outcome(http::outcome_code::invalid_state, "response_writer: writer is closed");
    }
    if (!started_ || sink_ == nullptr) {
        return http::outcome(http::outcome_code::invalid_state, "response_writer: response not started");
    }
    if (empty_chunk) {
        return http::outcome(http::outcome_code::invalid_argument, "response_writer: chunk is empty");
    }
    if (finished_) {
        return http::outcome(http::outcome_code::invalid_state, "response_writer: body already finished");
    }
    if (failed_) {
        return failure_;
    }
    return http::outcome::okay();
}

auto response_writer::fail_sticky(const http::outcome& reason) noexcept -> void {
    failed_ = true;
    failure_ = reason;
}

}  // namespace httpserver
