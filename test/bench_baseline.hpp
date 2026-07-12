/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

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
// TASK-083: per-platform warm-path baselines for bench_warm_path.cpp.
//
// bench_warm_path measures six per-request hot-path operations (see the
// file header in bench_warm_path.cpp). Each measurement now carries a
// pass/fail gate: a median that regresses more than kAllowedRegressionRatio
// over the platform baseline below fails the bench (rc=1). This is the
// ">= 5% improvement vs baseline" acceptance from TASK-058, hardened by
// TASK-083 into a real CI gate (the spec phrases it as fail-on-regression:
// the warm path must not get >5% slower than the committed numbers).
//
// HOW TO REFRESH (owned by TASK-084):
//   These are absolute ns/call medians captured once on a quiet reference
//   host, NOT recomputed at build time. When the CI runner hardware
//   changes, re-measure with `make bench` (release build, no sanitizers,
//   machine otherwise idle), take the bench_warm_path medians, pad by
//   ~25% to absorb runner jitter, and update the matching platform arm
//   below. TASK-084 explicitly owns the refresh cadence; see its task
//   body and test/PERFORMANCE.md for the procedure.
//
// Reference environment for the __APPLE__ arm:
//   * host triple   : aarch64-apple-darwin25.x (Apple silicon)
//   * compiler      : Apple clang 21.x
//   * C++ stdlib    : libc++ (LLVM)
//   * build profile : -std=c++20 -O3 (release; no sanitizers)
//
// The Linux/libstdc++ and MSVC arms carry conservative placeholder values
// (TODO(TASK-084)) until they are re-measured on their respective CI
// runners. They are set deliberately loose so the gate never produces a
// false failure before TASK-084 calibrates them; they are NOT a tight
// regression bound on those platforms yet.

#ifndef TEST_BENCH_BASELINE_HPP_
#define TEST_BENCH_BASELINE_HPP_

namespace httpserver::bench_baseline {

#if defined(__APPLE__)
// Apple-silicon reference medians (padded ~25% over observed values).
// Observed on the maintainer host: 12.7 / 102.7 / 1.24 / 30.2 / 517 / 505.
inline constexpr double WARM_CANONICALIZE_NS              = 16.0;
inline constexpr double WARM_SHOULD_SKIP_AUTH_NONEMPTY_NS = 130.0;
inline constexpr double WARM_SHOULD_SKIP_AUTH_EMPTY_NS    = 2.0;
inline constexpr double WARM_SERIALIZE_ALLOW_405_NS       = 40.0;
inline constexpr double WARM_BUILD_REQUEST_ARGS_PCT2F_NS  = 650.0;
inline constexpr double WARM_BUILD_REQUEST_ARGS_PLAIN_NS  = 640.0;
#elif defined(__linux__)
// Any C++ stdlib on Linux. TODO(TASK-084): re-measure on the
// verify-build.yml runner and tighten. Placeholders are ~3x the
// apple-silicon medians so the gate cannot false-fail before calibration.
inline constexpr double WARM_CANONICALIZE_NS              = 48.0;
inline constexpr double WARM_SHOULD_SKIP_AUTH_NONEMPTY_NS = 390.0;
inline constexpr double WARM_SHOULD_SKIP_AUTH_EMPTY_NS    = 6.0;
inline constexpr double WARM_SERIALIZE_ALLOW_405_NS       = 120.0;
inline constexpr double WARM_BUILD_REQUEST_ARGS_PCT2F_NS  = 1950.0;
inline constexpr double WARM_BUILD_REQUEST_ARGS_PLAIN_NS  = 1920.0;
#elif defined(_WIN32)
// MSVC STL. TODO(TASK-084): re-measure on a Windows runner and tighten.
// Placeholders mirror the Linux conservative arm.
inline constexpr double WARM_CANONICALIZE_NS              = 48.0;
inline constexpr double WARM_SHOULD_SKIP_AUTH_NONEMPTY_NS = 390.0;
inline constexpr double WARM_SHOULD_SKIP_AUTH_EMPTY_NS    = 6.0;
inline constexpr double WARM_SERIALIZE_ALLOW_405_NS       = 120.0;
inline constexpr double WARM_BUILD_REQUEST_ARGS_PCT2F_NS  = 1950.0;
inline constexpr double WARM_BUILD_REQUEST_ARGS_PLAIN_NS  = 1920.0;
#else
#error "bench_baseline.hpp: no warm-path baseline for this platform; re-measure with `make bench` and add an arm (see TASK-084)."
#endif

// Allowed regression before the bench fails: a median may be up to 5%
// slower than the committed baseline. The bench fails when
//     measured > baseline * kAllowedRegressionRatio.
// 5% per TASK-058 acceptance / TASK-083 spec.
inline constexpr double kAllowedRegressionRatio = 1.05;

}  // namespace httpserver::bench_baseline

#endif  // TEST_BENCH_BASELINE_HPP_
