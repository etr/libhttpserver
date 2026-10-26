/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino
     SPDX-License-Identifier: LGPL-2.1-or-later
*/
#include <httpserver/detail/tls_psk_runtime.hpp>
#include <condition_variable>
#include <atomic>
#include <cstdio>
#include <cinttypes>
#include <functional>
#include <deque>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>
#include <httpserver/server/options.hpp>
namespace httpserver::detail {
namespace {
using clock = std::chrono::steady_clock;
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
void capture_runtime(const char* label, const char* phase, const void* identity, std::size_t admitted,
                     std::size_t queued, std::size_t live, int status = 0) {
    static std::atomic<unsigned> rows{0};
    const auto row = rows.fetch_add(1);
    if (row < 2048) {
        std::fprintf(stderr, "%s phase=%s id=%p thread=%zu admitted=%zu queued=%zu live=%zu status=%d\n",
            label, phase, identity, std::hash<std::thread::id>{}(std::this_thread::get_id()), admitted, queued, live, status);
    } else if (row == 2048) {
        std::fprintf(stderr, "PSK_RUNTIME truncated=1\n");
    }
}
void capture_lookup(const char* phase, const void* identity, const psk_handshake_context& context, int status) {
    static std::atomic<unsigned> rows{0};
    const auto row = rows.fetch_add(1);
    if (row < 512) {
        std::fprintf(stderr, "PSK_LOOKUP phase=%s id=%p thread=%zu generation=%" PRIu64 " version=%d status=%d\n",
            phase, identity, std::hash<std::thread::id>{}(std::this_thread::get_id()),
            static_cast<std::uint64_t>(context.credential_generation), static_cast<int>(context.version), status);
    } else if (row == 512) {
        std::fprintf(stderr, "PSK_LOOKUP truncated=1\n");
    }
}
#endif
std::thread start_thread(const tls_psk_runtime::thread_start& start, executor::handler work) {
    return start ? start(std::move(work)) : std::thread(std::move(work));
}
// This observer owns no provider, application or lane references. Its fixed
// handles are all released before publishing provider-native retirement.
struct provider_retirement : std::enable_shared_from_this<provider_retirement> {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::unique_ptr<std::thread>> workers;
    bool registration_closed = false;
    bool retired = false;
    bool failed = false;
    explicit provider_retirement(std::size_t count) { workers.reserve(count); }
    static std::shared_ptr<provider_retirement> create(std::size_t count, const tls_psk_runtime::thread_start& start) {
        auto state = std::make_shared<provider_retirement>(count);
        start_thread(start, [state] { state->observe(); }).detach();
        return state;
    }
    void add(std::unique_ptr<std::thread> worker) {
        std::lock_guard lock(mutex);
        workers.push_back(std::move(worker));  // Capacity was reserved before startup.
    }
    void seal() {
        std::lock_guard lock(mutex);
        registration_closed = true;
        changed.notify_all();
    }
    static bool join_worker(std::unique_ptr<std::thread>& worker) noexcept {
        try {
            worker->join();
            return true;
        } catch (...) {
            // A distinct observer owns valid handles exactly once. If the OS
            // nevertheless refuses join, fail retirement without terminating.
            try {
                if (worker->joinable()) worker->detach();
            } catch (...) {
                // Preserve the bounded handle if the OS also refuses detach;
                // destroying a joinable thread would terminate the process.
                static_cast<void>(worker.release());
            }
            return false;
        }
    }
    void observe() {
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] { return registration_closed; });
        }
        bool success = true;
        for (auto& worker : workers) success = join_worker(worker) && success;
        workers.clear();
        std::lock_guard lock(mutex);
        retired = success;
        failed = !success;
        changed.notify_all();
    }
    bool drain(clock::time_point deadline) {
        std::unique_lock lock(mutex);
        changed.wait_until(lock, deadline, [&] { return retired || failed; });
        return retired;
    }
};
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
    std::shared_ptr<provider_retirement> retirement;
    std::stop_source cancellation;
    void start_worker(const tls_psk_runtime::thread_start& start) {
        auto handle = std::make_unique<std::thread>();
        { std::lock_guard lock(mutex); ++live; }
        try {
            *handle = start_thread(start, [self = shared_from_this()] { self->run(); });
        } catch (...) {
            std::lock_guard lock(mutex);
            --live;
            throw;
        }
        if (retirement) retirement->add(std::move(handle));
        else handle->detach();
    }
    void start(std::size_t workers, std::size_t queued, const tls_psk_runtime::thread_start& start, bool provider) {
        capacity = workers + queued;
        if (provider) retirement = provider_retirement::create(workers, start);
        try {
            for (std::size_t i = 0; i < workers; ++i) start_worker(start);
        } catch (...) {
            stop();
            if (retirement) retirement->seal();
            throw;
        }
        if (retirement) retirement->seal();
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
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
            std::size_t capture_admitted, capture_queued, capture_live;
#endif
            {
                std::lock_guard lock(mutex);
                --admitted;
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
                capture_admitted = admitted; capture_queued = queue.size(); capture_live = live;
#endif
            }
            changed.notify_all();
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
            capture_runtime("PSK_LANE", "retired", this, capture_admitted, capture_queued, capture_live);
#endif
            // The completed provider step no longer owns admission. Its owner
            // may execute inline and immediately submit the next step.
            try {
                if (current.completed) current.completed();
            } catch (...) { /* Never propagate into the worker. */ }
        }
    }
    http::outcome_code submit(executor::handler work, executor::handler completed = {}) {
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
        std::size_t capture_admitted, capture_queued, capture_live;
#endif
        {
            std::lock_guard lock(mutex);
            if (cancellation.stop_requested()) return http::outcome_code::cancelled;
            if (admitted >= capacity) return http::outcome_code::limit_exceeded;
            queue.push_back({std::move(work), std::move(completed)});
            ++admitted;
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
            capture_admitted = admitted; capture_queued = queue.size(); capture_live = live;
#endif
        }
        changed.notify_one();
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
        capture_runtime("PSK_LANE", "admitted", this, capture_admitted, capture_queued, capture_live);
#endif
        return http::outcome_code::ok;
    }
    void stop() {
        cancellation.request_stop();
        // Pair with the wait mutex to prevent a missed stop notification.
        { std::lock_guard lock(mutex); }
        changed.notify_all();
    }
    bool drain(clock::time_point deadline) {
        if (retirement) return retirement->drain(deadline);
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
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
            capture_lookup("callback-enter", completed.get(), context, static_cast<int>(result.status));
#endif
            result = callback(owned, context);
            if (result.status == psk_lookup_status::accepted && (result.key.bytes().empty() || result.key.bytes().size() > context.maximum_key_bytes)) {
                result.status = psk_lookup_status::rejected;
            }
        } catch (...) { result.status = psk_lookup_status::provider_failure; }
    }
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
    capture_lookup("callback-return", completed.get(), context, static_cast<int>(result.status));
#endif
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
        options.handshake_timeout <= std::chrono::milliseconds::zero() || options.handshake_timeout > server::max_timeout) {
        throw std::invalid_argument("TLS PSK runtime invalid");
    }
}
}  // namespace
struct tls_psk_runtime::impl {
    tls_psk_runtime_options options;
    std::shared_ptr<lane> handshake = std::make_shared<lane>();
    std::shared_ptr<lane> lookup = std::make_shared<lane>();
    explicit impl(tls_psk_runtime_options value, const tls_psk_runtime::thread_start& start) : options(value) {
        validate(options);
        handshake->start(options.handshake_workers, options.handshake_queue, start, true);
        try {
            lookup->start(options.lookup_workers, options.lookup_queue, start, false);
        } catch (...) {
            handshake->stop();
            throw;
        }
    }
};
tls_psk_runtime::tls_psk_runtime(tls_psk_runtime_options options, thread_start start) : impl_(std::make_shared<impl>(options, start)) {}
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
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
    capture_lookup("submitted", completed.get(), context, static_cast<int>(admitted));
#endif
    auto result = wait_lookup(completed, context, external_stop, stop);
#if defined(LIBHTTPSERVER_REMAINING_CAPTURE)
    capture_lookup("wait-return", completed.get(), context, static_cast<int>(result.status));
#endif
    return result;
}
void tls_psk_runtime::stop() { impl_->handshake->stop(); impl_->lookup->stop(); }
bool tls_psk_runtime::drain(clock::time_point deadline) {
    const bool handshake = impl_->handshake->drain(deadline);
    const bool lookup = impl_->lookup->drain(deadline);
    return handshake && lookup;
}
}  // namespace httpserver::detail
