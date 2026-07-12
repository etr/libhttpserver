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
// The bench measures the steady-state (warm-cache) cost: the path a real
// consumer sees after the first call populates the memoised view map.
// On failure (ratio < 10x) the binary prints the diagnostic and returns 1,
// failing `make bench`.
//
// Sanitizer builds are skipped at runtime (see detect_sanitizer_build())
// because ASan/MSan/TSan inflate per-call cost 10-50x relative to the
// release-mode v1 baseline. The skip emits a "SKIP:" prefixed message.
//
// Wired into `make bench` via `EXTRA_PROGRAMS` in test/Makefile.am;
// NOT part of `make check`. See test/PERFORMANCE.md for methodology.

#include <cstdio>

#include "bench_harness.hpp"  // NOLINT(build/include_subdir)
#include "httpserver/create_test_request.hpp"
#include "httpserver/http_request.hpp"
#include "v1_baseline/v1_constants.hpp"

using httpserver::create_test_request;
using httpserver::http_request;
using httpserver::v1_baseline::V1_GET_HEADERS_NS_PER_CALL;

// TASK-084: pin the per-stdlib provenance of the v1 baseline at compile
// time. Before TASK-084, V1_GET_HEADERS_NS_PER_CALL was a single
// mono-platform literal (760.0, the libc++/Apple-Silicon measurement)
// reused unchanged on libstdc++/Linux. These static_asserts encode that
// the constant is now selected per-stdlib and carries the platform's own
// re-measured value, so a future edit cannot silently revert the
// libstdc++ arm to the reused libc++ number. The arm for the inactive
// stdlib is not compiled, so each lane only checks its own value.
#if defined(__GLIBCXX__)
static_assert(
    V1_GET_HEADERS_NS_PER_CALL != 760.0,
    "libstdc++ v1 get_headers baseline must be the re-measured libstdc++ "
    "value (TASK-084), not the reused libc++ 760ns literal");
// 500.0 is ~25% below the committed 640.0 value, giving re-measurement
// latitude while still catching an accidental reversion to a near-zero
// placeholder; 760.0 matches the libc++ literal, but the preceding != assert
// already rejects an exact copy-paste, so this range only needs to bound
// plausible libstdc++ measurements.
static_assert(
    V1_GET_HEADERS_NS_PER_CALL >= 500.0
        && V1_GET_HEADERS_NS_PER_CALL <= 760.0,
    "libstdc++ v1 get_headers baseline must lie within the re-measured "
    "band (TASK-084); a value outside it signals an un-re-measured constant");
#elif defined(_LIBCPP_VERSION)
static_assert(
    V1_GET_HEADERS_NS_PER_CALL == 760.0,
    "libc++ v1 get_headers baseline must remain the original TASK-039 "
    "measurement (760.0 ns), the conservative lower end of 756..784 ns");
#endif

// Compile-time detection of any sanitizer instrumentation that
// would distort the per-call cost beyond recognition.
// Wrapped in a constexpr function to avoid the awkward trailing-semicolon
// pattern that multi-line conditional #if initialisers otherwise require.
static constexpr bool detect_sanitizer_build() {
#if defined(__SANITIZE_ADDRESS__) \
    || defined(__SANITIZE_THREAD__) \
    || defined(__SANITIZE_MEMORY__) \
    || defined(__SANITIZE_HWADDRESS__)
    return true;
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer) \
      || __has_feature(thread_sanitizer) \
      || __has_feature(memory_sanitizer) \
      || __has_feature(undefined_behavior_sanitizer)
    return true;
#  else
    return false;
#  endif
#else
    return false;
#endif
}
static constexpr bool kSanitizerBuild = detect_sanitizer_build();

int main() {
    if constexpr (kSanitizerBuild) {
        // "SKIP:" sentinel prefix lets scripted harnesses distinguish a
        // genuine skip from a PASS, and distinguishes it from silent exit 0.
        // PERFORMANCE.md documents that bench results from sanitizer builds
        // must be ignored.
        std::printf("SKIP: bench_get_headers: sanitizer build detected; "
                    "per-call cost is inflated 10-50x and cannot be compared "
                    "to the release-mode v1 baseline.\n");
        return 0;
    }

    // Build a 16-header request; first call to get_headers() warms the cache.
    // Subsequent calls exercise the warm (memoised) path — the path
    // PRD-REQ-REQ-001 claims is >=10x faster than v1.
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

    constexpr int OUTER = 11;
    constexpr int INNER = 1'000'000;
    // run_bench_median times OUTER*INNER calls and returns the median
    // ns/call. OUTER=11 (odd) ensures samples[OUTER/2] is the true
    // middle element.
    const double v2_median_ns = run_bench_median([&]() {
        const auto& h = req.get_headers();
        do_not_optimize(h);
    }, OUTER, INNER);
    const double ratio = V1_GET_HEADERS_NS_PER_CALL / v2_median_ns;

    std::printf("bench_get_headers v1=%.3fns v2=%.3fns ratio=%.2fx "
                "(median over %d reps x %d iters)\n",
                V1_GET_HEADERS_NS_PER_CALL, v2_median_ns, ratio, OUTER, INNER);

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
