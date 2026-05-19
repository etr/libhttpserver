/*
     This file is part of libhttpserver
     Copyright (C) 2011-2019 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/
// TASK-039 Cycle B: runtime microbenchmark verifying that v2.0's
// `http_request::get_headers()` is at least 10x faster than v1's
// (PRD-REQ-REQ-001 / PRD §3.6 acceptance).
//
// v2.0's `get_headers()` returns a const reference to a memoised
// `http::header_view_map`; first call populates the cache, all
// subsequent calls are a branch + reference return. The bench warms
// the cache up front, then times the hot path over 11 outer reps x
// 1,000,000 inner iterations and compares the median ns/call to the
// v1 baseline literal V1_GET_HEADERS_NS_PER_CALL.
// OUTER=11 (odd) so samples_ns[OUTER/2] = samples_ns[5] is the true
// median element with no tie-breaking required.
//
// On failure (ratio < 10x) the binary prints the diagnostic and
// returns 1, failing `make bench`.
//
// On sanitizer builds (ASan / UBSan / TSan / MSan) the per-call cost
// is inflated by 10..50x; comparing to a release-mode baseline would
// produce a false negative. The bench detects sanitizer instrumentation
// at compile time and exits 0 with a skip message so `make bench`
// remains green.
//
// Wired into `make bench` via `EXTRA_PROGRAMS` in test/Makefile.am;
// NOT part of `make check`.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "httpserver/create_test_request.hpp"
#include "httpserver/http_request.hpp"
#include "v1_baseline/v1_constants.hpp"

using httpserver::create_test_request;
using httpserver::http_request;
using httpserver::v1_baseline::V1_GET_HEADERS_NS_PER_CALL;

// Defeat dead-store elimination. We feed the const reference's
// address through an asm-volatile memory clobber so the compiler
// can't elide the get_headers() call.
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(&value) : "memory");
#else
    // MSVC fallback: take address via volatile sink.
    volatile const void* sink = static_cast<const void*>(&value);
    (void)sink;
#endif
}

// Compile-time detection of any sanitizer instrumentation that
// would distort the per-call cost beyond recognition.
static constexpr bool kSanitizerBuild =
#if defined(__SANITIZE_ADDRESS__) \
    || defined(__SANITIZE_THREAD__) \
    || defined(__SANITIZE_MEMORY__) \
    || defined(__SANITIZE_HWADDRESS__)
    true
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer) \
      || __has_feature(thread_sanitizer) \
      || __has_feature(memory_sanitizer) \
      || __has_feature(undefined_behavior_sanitizer)
    true
#  else
    false
#  endif
#else
    false
#endif
    ;  // NOLINT(whitespace/semicolon)

int main() {
    if constexpr (kSanitizerBuild) {
        std::printf("bench_get_headers: skipped (sanitizer build "
                    "would distort ns/call)\n");
        return 0;
    }

    // Build a 16-header request via create_test_request. The fixture
    // populates `headers_local`; the first call to get_headers() on
    // a connection-less request flows through the
    // `connection_ == nullptr` branch in ensure_headerlike_cache,
    // builds the view map, and sets headers_cache_built_=true.
    // Subsequent calls return the cached reference (the warm path).
    create_test_request b;
    for (int i = 0; i < 16; ++i) {
        char k[32];
        char v[32];
        std::snprintf(k, sizeof(k), "X-Bench-%02d", i);
        std::snprintf(v, sizeof(v), "v%02d", i);
        b.header(k, v);
    }
    http_request req = b.build();

    // Warmup: populate cache + warm icache + warm branch predictor.
    for (int i = 0; i < 10'000; ++i) {
        const auto& h = req.get_headers();
        do_not_optimize(h);
    }

    using clock = std::chrono::steady_clock;
    constexpr int OUTER = 11;
    constexpr int INNER = 1'000'000;
    std::vector<double> samples_ns;
    samples_ns.reserve(OUTER);
    for (int r = 0; r < OUTER; ++r) {
        auto t0 = clock::now();
        for (int i = 0; i < INNER; ++i) {
            const auto& h = req.get_headers();
            do_not_optimize(h);
        }
        auto t1 = clock::now();
        const double ns_per_call =
            std::chrono::duration<double, std::nano>(t1 - t0).count() / INNER;
        samples_ns.push_back(ns_per_call);
    }
    std::sort(samples_ns.begin(), samples_ns.end());
    const double v2_median_ns = samples_ns[OUTER / 2];
    const double v1_ns = V1_GET_HEADERS_NS_PER_CALL;
    const double ratio = v1_ns / v2_median_ns;

    std::printf("bench_get_headers v1=%.3fns v2=%.3fns ratio=%.2fx "
                "(min=%.3fns max=%.3fns over %d reps x %d iters)\n",
                v1_ns, v2_median_ns, ratio,
                samples_ns.front(), samples_ns.back(), OUTER, INNER);

    constexpr double kMinRatio = 10.0;
    if (ratio < kMinRatio) {
        std::printf("FAIL: v2.0 get_headers() is only %.2fx faster than v1; "
                    "PRD-REQ-REQ-001 / PRD §3.6 requires >= %.1fx\n",
                    ratio, kMinRatio);
        return 1;
    }
    std::printf("PASS: ratio %.2fx >= %.1fx\n", ratio, kMinRatio);
    return 0;
}
