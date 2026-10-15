/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#include <httpserver/body_reader.hpp>
#include <utility>
#include <vector>

namespace httpserver::detail {

auto wait_node::claim() noexcept -> bool {
    bool expected = false;
    return delivered.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

auto wait_node::resume_posted() noexcept -> void {
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

body_wait::body_wait(body_source* source, stop_token cancel) : source_(source), node_(std::make_shared<wait_node>()), cancel_(std::move(cancel)) {}

auto body_wait::bind_frame(task_frame_base* frame) noexcept -> body_wait& {
    frame_ = frame;
    return *this;
}

auto body_wait::await_suspend(std::coroutine_handle<> awaiting) -> std::coroutine_handle<> {
    node_->awaiting = awaiting;
    node_->witness = frame_ ? frame_->frame_witness_ptr() : nullptr;
    node_->target = frame_ ? frame_->frame_executor() : current_executor();
    source_->park(*this);
    // Seam contract: completion is posted to this executor, never
    // resumed inline on the parking (or completing) thread.
    return std::noop_coroutine();
}

auto body_wait::complete(body_wake wake) noexcept -> void {
    if (!node_->claim()) return;
    node_->result = wake;
    node_->resume_posted();
}

body_wait::~body_wait() {
    if (!node_->delivered.load(std::memory_order_acquire)) {
        source_->unpark(*this);
    }
}

}  // namespace httpserver::detail

namespace httpserver {

auto body_reader::read_some(std::span<std::byte> destination) -> task<body_read> {
    const http::outcome gate = enter(destination.empty());
    if (!gate.ok()) {
        co_return body_read{gate, {}, false};
    }
    if (bodyless()) {
        // Admitted with no engine source: the request is bodyless,
        // so the body is empty and already complete.
        at_end_ = true;
        co_return body_read{http::outcome::okay(), {}, true};
    }
    pending_ = true;
    for (;;) {
        const detail::body_pull_result pulled = source_->pull(destination);
        if (pulled.kind == detail::body_pull::data) {
            if (pulled.copied == 0) {
                // Forbidden zero-byte data pull (engine misuse):
                // never surface an empty data span; re-pull.
                continue;
            }
            pending_ = false;
            co_return body_read{http::outcome::okay(), destination.first(pulled.copied), false};
        }
        if (pulled.kind == detail::body_pull::end) {
            at_end_ = true;
            pending_ = false;
            co_return body_read{http::outcome::okay(), {}, true};
        }
        if (pulled.kind == detail::body_pull::failed) {
            fail_sticky(source_->failure());
            pending_ = false;
            co_return body_read{failure_, {}, false};
        }
        // Nothing staged: park until the first of bytes staged,
        // end, failure, or exchange disconnect.
        detail::body_wait wait(source_, cancel_token_);
        if (co_await wait == detail::body_wake::cancelled) {
            pending_ = false;
            co_return body_read{http::outcome(http::outcome_code::cancelled, "body_reader: read cancelled by disconnect"), {}, false};
        }
    }
}

auto body_reader::trailers() const noexcept -> const http::fields& {
    if (source_ != nullptr) return source_->trailers();
    static const http::fields none;
    return none;
}

auto body_reader::collect(std::uint64_t maximum) -> task<body_collect> {
    const http::outcome gate = enter(false);
    if (!gate.ok()) {
        co_return body_collect{gate, {}};
    }
    if (bodyless()) {
        at_end_ = true;
        co_return body_collect{http::outcome::okay(), {}};
    }
    pending_ = true;
    std::vector<std::byte> out;
    std::byte scratch[COLLECT_CHUNK_BYTES];
    for (;;) {
        const std::uint64_t room = maximum - static_cast<std::uint64_t>(out.size());
        const detail::body_pull_result pulled = source_->pull(std::span<std::byte>(scratch, collect_want(room)));
        if (pulled.kind == detail::body_pull::data && pulled.copied > 0) {
            if (pulled.copied > room) {
                // Exact outcome: the bytes beyond the cap prove the
                // over-limit body; the tripping pull is discarded,
                // never appended.
                fail_sticky(http::outcome(http::outcome_code::limit_exceeded, "body_reader: body exceeds the collect() cap of " + std::to_string(maximum) + " bytes"));
                pending_ = false;
                co_return body_collect{failure_, {}};
            }
            out.insert(out.end(), scratch, scratch + pulled.copied);
            continue;
        }
        if (pulled.kind == detail::body_pull::end) {
            at_end_ = true;
            pending_ = false;
            co_return body_collect{http::outcome::okay(), std::move(out)};
        }
        if (pulled.kind == detail::body_pull::failed) {
            fail_sticky(source_->failure());
            pending_ = false;
            co_return body_collect{failure_, {}};
        }
        // Nothing staged: park until the first of bytes staged,
        // end, failure, or exchange disconnect.
        detail::body_wait wait(source_, cancel_token_);
        if (co_await wait == detail::body_wake::cancelled) {
            pending_ = false;
            co_return body_collect{http::outcome(http::outcome_code::cancelled, "body_reader: read cancelled by disconnect"), {}};
        }
    }
}

auto body_reader::activate(detail::body_source* source) noexcept -> void {
    source_ = source;
    admitted_ = true;
}

auto body_reader::enter(bool empty_destination) const -> http::outcome {
    if (pending_) {
        return http::outcome(http::outcome_code::invalid_state, "body_reader: a read is already outstanding");
    }
    if (cancel_token_.stop_requested()) {
        return http::outcome(http::outcome_code::connection_closed, disconnect_reason_.message());
    }
    if (closed_) {
        return http::outcome(http::outcome_code::invalid_state, "body_reader: exchange is terminal");
    }
    if (!admitted_) {
        return http::outcome(http::outcome_code::invalid_state, "body_reader: body not admitted");
    }
    if (empty_destination) {
        return http::outcome(http::outcome_code::invalid_argument, "body_reader: destination buffer is empty");
    }
    if (at_end_) {
        return http::outcome(http::outcome_code::invalid_state, "body_reader: body already at end");
    }
    if (failed_) {
        return failure_;
    }
    return http::outcome::okay();
}

auto body_reader::fail_sticky(const http::outcome& reason) noexcept -> void {
    failed_ = true;
    failure_ = reason;
}

}  // namespace httpserver
