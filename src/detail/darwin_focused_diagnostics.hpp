/* Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SRC_DETAIL_DARWIN_FOCUSED_DIAGNOSTICS_HPP_
#define SRC_DETAIL_DARWIN_FOCUSED_DIAGNOSTICS_HPP_
#include <cstdio>
// Temporary, noninstalled instrumentation for one authorized Darwin batch.
#ifdef LHS_DARWIN_FOCUSED_DIAGNOSTICS
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
// One bounded write, no allocator, retry, new mutex or retained event state.
#define LHS_DARWIN_DIAG(...) do { \
    const int lhs_saved_errno = errno; \
    char lhs_line[512]; \
    const int lhs_length = std::snprintf(lhs_line, sizeof(lhs_line), __VA_ARGS__); \
    if (lhs_length > 0) { \
        const auto lhs_count = static_cast<std::size_t>(lhs_length) < sizeof(lhs_line) \
            ? static_cast<std::size_t>(lhs_length) : sizeof(lhs_line) - 1; \
        static_cast<void>(::write(STDERR_FILENO, lhs_line, lhs_count)); \
    } \
    errno = lhs_saved_errno; \
} while (false)
#define LHS_DARWIN_HEADER_DIAG(...) do { \
    const int lhs_trace_errno = errno; \
    if (std::getenv("LHS_DARWIN_HEADER_TRACE")) { \
        LHS_DARWIN_DIAG(__VA_ARGS__); \
    } \
    errno = lhs_trace_errno; \
} while (false)
#define LHS_DARWIN_STORAGE_DIAG(...) do { \
    const int lhs_storage_errno = errno; \
    if (std::getenv("LHS_DARWIN_STORAGE_TRACE")) { \
        LHS_DARWIN_DIAG(__VA_ARGS__); \
    } \
    errno = lhs_storage_errno; \
} while (false)
// Match printf's ll formats on both LP64 and LLP64 hosts.
#define LHS_DARWIN_ID(value) static_cast<unsigned long long>(value)  // NOLINT(runtime/int)
#define LHS_DARWIN_TICK(point) \
    static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>((point).time_since_epoch()).count())  // NOLINT(runtime/int)
#define LHS_DARWIN_NOW LHS_DARWIN_TICK(std::chrono::steady_clock::now())
#else
#define LHS_DARWIN_DIAG(...) do {} while (false)
#define LHS_DARWIN_HEADER_DIAG(...) do {} while (false)
#define LHS_DARWIN_STORAGE_DIAG(...) do {} while (false)
#endif
#endif  // SRC_DETAIL_DARWIN_FOCUSED_DIAGNOSTICS_HPP_
