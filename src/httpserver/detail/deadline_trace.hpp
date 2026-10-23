/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_HTTPSERVER_DETAIL_DEADLINE_TRACE_HPP_
#define SRC_HTTPSERVER_DETAIL_DEADLINE_TRACE_HPP_

// Temporary native Darwin diagnostic; each call site emits at most 64 records.
#if defined(HTTPSERVER_DIAG_HEADER_DEADLINE)
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>

namespace httpserver::detail {
using deadline_trace_id = unsigned long long;  // NOLINT(runtime/int)
inline long long deadline_trace_ms(std::chrono::milliseconds time) { return time.count(); }  // NOLINT(runtime/int)
inline long long deadline_trace_ns(std::chrono::steady_clock::time_point time) {  // NOLINT(runtime/int)
    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
}
}  // namespace httpserver::detail

#define V3_DEADLINE_TRACE(...) do { \
    static std::atomic<unsigned> trace_count{0}; \
    if (trace_count.fetch_add(1, std::memory_order_relaxed) < 64) { \
        const int trace_errno = errno; \
        std::fprintf(stderr, "V3_DEADLINE " __VA_ARGS__); /* NOLINT(build/include_what_you_use) */ \
        errno = trace_errno; \
    } \
} while (false)
#else
#define V3_DEADLINE_TRACE(...) do { } while (false)
#endif

#endif  // SRC_HTTPSERVER_DETAIL_DEADLINE_TRACE_HPP_
