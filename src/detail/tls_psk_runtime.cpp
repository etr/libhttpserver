/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/tls_psk_runtime.hpp>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>
namespace httpserver::detail {
namespace {
using clock = std::chrono::steady_clock;
struct lane : std::enable_shared_from_this<lane> {
    struct job {
        executor::handler work;
        executor::handler completed;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<job> queue;
    std::size_t admitted = 0;
    std::size_t live = 0;
    std::size_t capacity = 0;
    std::stop_source cancellation;
    void start(std::size_t workers, std::size_t queued) {
        capacity = workers + queued;
        for (std::size_t i = 0; i < workers; ++i) {
            ++live;
            try {
                std::thread([self = shared_from_this()] { self->run(); }).detach();
            } catch (...) {
                --live;
                stop();
                throw;
            }
        }
    }
    void run() {
        for (;;) {
            job current;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return cancellation.stop_requested() || !queue.empty(); });
                if (queue.empty() && cancellation.stop_requested()) {
                    --live;
                    changed.notify_all();
                    return;
                }
                current = std::move(queue.front());
                queue.pop_front();
            }
            // Internal jobs own completion cleanup, including when stopped.
            try {
                current.work();
            } catch (...) { /* Never propagate into the worker. */ }
            current.work = nullptr;
            {
                std::lock_guard lock(mutex);
                --admitted;
            }
            changed.notify_all();
            // The completed provider step no longer owns admission. Its owner
            // may execute inline and immediately submit the next step.
            try {
                if (current.completed) current.completed();
            } catch (...) { /* Never propagate into the worker. */ }
        }
    }
    http::outcome_code submit(executor::handler work, executor::handler completed = {}) {
        {
            std::lock_guard lock(mutex);
            if (cancellation.stop_requested()) return http::outcome_code::cancelled;
            if (admitted >= capacity) return http::outcome_code::limit_exceeded;
            queue.push_back({std::move(work), std::move(completed)});
            ++admitted;
        }
        changed.notify_one();
        return http::outcome_code::ok;
    }
    void stop() {
        cancellation.request_stop();
        // Pair with the wait mutex to prevent a missed stop notification.
        { std::lock_guard lock(mutex); }
        changed.notify_all();
    }
    bool drain(clock::time_point deadline) {
        std::unique_lock lock(mutex);
        return changed.wait_until(lock, deadline, [&] { return admitted == 0 && live == 0; });
    }
};
struct completion {
    std::mutex mutex;
    std::condition_variable changed;
    std::stop_source cancellation;
    bool ready = false;
    bool retired = false;
    psk_lookup_result result;
};
psk_lookup_status unavailable(const psk_handshake_context& context, std::stop_token runtime_stop) {
    if (context.cancellation.stop_requested() || runtime_stop.stop_requested()) return psk_lookup_status::cancelled;
    if (clock::now() >= context.deadline) return psk_lookup_status::timeout;
    return psk_lookup_status::accepted;
}
void execute_lookup(const std::shared_ptr<completion>& completed, const psk_lookup& callback,
                    const std::vector<std::byte>& owned, const psk_handshake_context& context, std::stop_token stop) {
    psk_lookup_result result;
    result.status = unavailable(context, stop);
    if (result.status == psk_lookup_status::accepted) {
        try {
            result = callback(owned, context);
            if (result.status == psk_lookup_status::accepted && (result.key.bytes().empty() || result.key.bytes().size() > context.maximum_key_bytes)) {
                result.status = psk_lookup_status::rejected;
            }
        } catch (...) { result.status = psk_lookup_status::provider_failure; }
    }
    if (result.status != psk_lookup_status::accepted) result.key.clear();
    {
        std::lock_guard lock(completed->mutex);
        if (!completed->retired) {
            completed->result = std::move(result);
            completed->ready = true;
        }
    }
    completed->changed.notify_all();
}
psk_lookup_result wait_lookup(const std::shared_ptr<completion>& completed, const psk_handshake_context& context,
                              std::stop_token external_stop, std::stop_token stop) {
    const auto wake = [completed] {
        completed->cancellation.request_stop();
        std::lock_guard lock(completed->mutex);
        completed->changed.notify_all();
    };
    std::stop_callback cancelled(external_stop, wake);
    std::stop_callback stopped(stop, wake);
    std::unique_lock lock(completed->mutex);
    completed->changed.wait_until(lock, context.deadline, [&] { return completed->ready || unavailable(context, stop) != psk_lookup_status::accepted; });
    const auto status = unavailable(context, stop);
    if (status != psk_lookup_status::accepted || !completed->ready) {
        completed->retired = true;
        completed->cancellation.request_stop();
        completed->result.key.clear();
        return {status == psk_lookup_status::accepted ? psk_lookup_status::timeout : status, {}};
    }
    return std::move(completed->result);
}
void validate(const tls_psk_runtime_options& options) {
    const auto workers = [](auto n) { return n > 0 && n <= 64; };
    if (!workers(options.handshake_workers) || !workers(options.lookup_workers) ||
        options.handshake_queue > 65536 || options.lookup_queue > 65536 ||
        options.handshake_timeout <= std::chrono::milliseconds::zero() || options.handshake_timeout > std::chrono::minutes(10)) {
        throw std::invalid_argument("TLS PSK runtime invalid");
    }
}
}  // namespace
struct tls_psk_runtime::impl {
    tls_psk_runtime_options options;
    std::shared_ptr<lane> handshake = std::make_shared<lane>();
    std::shared_ptr<lane> lookup = std::make_shared<lane>();
    explicit impl(tls_psk_runtime_options value) : options(value) {
        validate(options);
        handshake->start(options.handshake_workers, options.handshake_queue);
        try {
            lookup->start(options.lookup_workers, options.lookup_queue);
        } catch (...) {
            handshake->stop();
            throw;
        }
    }
};
tls_psk_runtime::tls_psk_runtime(tls_psk_runtime_options options) : impl_(std::make_shared<impl>(options)) {}
tls_psk_runtime::~tls_psk_runtime() { stop(); }
bool tls_psk_runtime::accepting() const { return !impl_->handshake->cancellation.stop_requested() && !impl_->lookup->cancellation.stop_requested(); }
std::chrono::milliseconds tls_psk_runtime::handshake_timeout() const { return impl_->options.handshake_timeout; }
http::outcome_code tls_psk_runtime::submit_handshake(executor::handler work, executor::handler completed) {
    return impl_->handshake->submit(std::move(work), std::move(completed));
}
psk_lookup_result tls_psk_runtime::lookup(psk_lookup callback, std::span<const std::byte> identity, psk_handshake_context context) {
    const auto stop = impl_->lookup->cancellation.get_token();
    if (const auto status = unavailable(context, stop); status != psk_lookup_status::accepted) {
        return {status, {}};
    }
    auto completed = std::make_shared<completion>();
    const auto external_stop = context.cancellation;
    context.cancellation = completed->cancellation.get_token();
    std::vector<std::byte> owned(identity.begin(), identity.end());
    const auto admitted = impl_->lookup->submit([completed, callback = std::move(callback), owned = std::move(owned), context, stop]() mutable {
        execute_lookup(completed, callback, owned, context, stop);
    });
    if (admitted != http::outcome_code::ok) return {admitted == http::outcome_code::cancelled ? psk_lookup_status::cancelled : psk_lookup_status::limit_exceeded, {}};
    return wait_lookup(completed, context, external_stop, stop);
}
void tls_psk_runtime::stop() { impl_->handshake->stop(); impl_->lookup->stop(); }
bool tls_psk_runtime::drain(clock::time_point deadline) {
    const bool handshake = impl_->handshake->drain(deadline);
    const bool lookup = impl_->lookup->drain(deadline);
    return handshake && lookup;
}
}  // namespace httpserver::detail
