/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_HTTPSERVER_DETAIL_DEADLINE_TRACE_HPP_
#define SRC_HTTPSERVER_DETAIL_DEADLINE_TRACE_HPP_

// Temporary native Darwin diagnostic; each site captures at most 64 records.
#if defined(HTTPSERVER_DIAG_HEADER_DEADLINE)
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <new>
#include <tuple>
#include <type_traits>

namespace httpserver::detail {
using deadline_trace_id = unsigned long long;  // NOLINT(runtime/int)
inline long long deadline_trace_ms(std::chrono::milliseconds time) { return time.count(); }  // NOLINT(runtime/int)
inline long long deadline_trace_ns(std::chrono::steady_clock::time_point time) {  // NOLINT(runtime/int)
    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
}

struct deadline_trace_record {
    long long monotonic_ns = 0;  // NOLINT(runtime/int)
    void (*write)(const deadline_trace_record&) = nullptr;
    alignas(std::max_align_t) std::byte payload[128]{};
    std::atomic<bool> ready{false};
};

// One buffer in the compiled library also receives the installed test's events.
deadline_trace_record* deadline_trace_reserve(long long monotonic_ns) noexcept;  // NOLINT(runtime/int)
void deadline_trace_flush() noexcept;  // Call only after all producers have stopped.

template<typename... Args>
void deadline_trace_capture(long long monotonic_ns, const char* format, Args... values) noexcept {  // NOLINT(runtime/int)
    struct payload_type {
        const char* format;
        std::tuple<Args...> values;  // NOLINT(build/include_what_you_use)
    };
    static_assert(sizeof(payload_type) <= sizeof(deadline_trace_record::payload));
    static_assert(alignof(payload_type) <= alignof(std::max_align_t));
    static_assert(std::is_trivially_destructible_v<payload_type>);
    if (auto* record = deadline_trace_reserve(monotonic_ns)) {
        new (record->payload) payload_type{format, {values...}};
        record->write = [](const deadline_trace_record& entry) {
            const auto& payload = *std::launder(reinterpret_cast<const payload_type*>(entry.payload));
            if constexpr (sizeof...(Args) == 0) {
                std::fputs(payload.format, stderr);  // NOLINT(build/include_what_you_use)
            } else {
                std::apply([&](auto... args) { std::fprintf(stderr, payload.format, args...); }, payload.values);  // NOLINT(build/include_what_you_use)
            }
        };
        record->ready.store(true, std::memory_order_release);
    }
}
}  // namespace httpserver::detail

#define V3_DEADLINE_TRACE(...) do { \
    static std::atomic<unsigned> trace_count{0}; \
    if (trace_count.fetch_add(1, std::memory_order_relaxed) < 64) { \
        const int trace_errno = errno; \
        const auto trace_ns = httpserver::detail::deadline_trace_ns(std::chrono::steady_clock::now()); \
        if constexpr (false) { std::fprintf(stderr, __VA_ARGS__); } /* NOLINT(build/include_what_you_use) */ \
        httpserver::detail::deadline_trace_capture(trace_ns, __VA_ARGS__); \
        errno = trace_errno; \
    } \
} while (false)
#else
#define V3_DEADLINE_TRACE(...) do { } while (false)
#endif

#endif  // SRC_HTTPSERVER_DETAIL_DEADLINE_TRACE_HPP_
