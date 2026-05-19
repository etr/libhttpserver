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
// TASK-039: v1 baseline literals captured from `master` for the
// performance acceptance gates in TASK-039.
//
// The values in this header are sampled ONCE on `master` (the v1.x
// release branch) and committed alongside the bench TUs that consume
// them. They are NOT computed at v2.0 build time because the v2.0
// `http_resource` no longer holds a `std::map<std::string, bool>` and
// v2.0's `get_headers()` does not exercise MHD's enumeration callback
// path at all -- there is no compile-time expression on the v2.0
// branch that reproduces the v1 numbers.
//
// Baseline environment (full details in test/PERFORMANCE.md):
//   * master SHA      : d8b055e ("Migrate to libmicrohttpd 1.0.0 API")
//   * host triple     : aarch64-apple-darwin25.3.0 (Apple silicon)
//   * compiler        : Apple clang 21.0.0
//   * C++ stdlib      : libc++ (LLVM)
//   * build profile   : -std=c++20 -O3 (release; no sanitizers)
//   * libmicrohttpd   : 1.0.5 (only relevant to ns/call measurement)
//
// Re-measurement: see test/v1_baseline/README.md.

#ifndef TEST_V1_BASELINE_V1_CONSTANTS_HPP_
#define TEST_V1_BASELINE_V1_CONSTANTS_HPP_

#include <cstddef>

namespace httpserver::v1_baseline {

// sizeof(httpserver::http_resource) on `master`, captured by
// test/v1_baseline/measure_v1_sizes.cpp.
//
// On libc++ (macOS) the layout is vptr (8) + std::map (24) = 32.
// On libstdc++ (Linux) the layout would be vptr (8) + std::map (48) = 56.
// We commit the libc++ value because that is the baseline-measurement
// stdlib used by the maintainer's reference host (see PERFORMANCE.md);
// the v2.0 check stays valid on libstdc++ because v2.0 shrinks even
// more aggressively there (v1 grows faster than v2 between stdlibs).
inline constexpr std::size_t V1_HTTP_RESOURCE_SIZEOF = 32;

// sizeof(std::map<std::string, bool>) on the same baseline host.
// libc++ reports 24 (three pointers: __begin_node_, __pair1_->__value_,
// __pair3_->__value_).
inline constexpr std::size_t V1_STD_MAP_STRING_BOOL_SIZEOF = 24;

// Median v1 ns/call for `http_request::get_headers()` against a
// 16-header request. Captured by
// test/v1_baseline/measure_v1_get_headers.cpp, 10 outer reps x
// 1,000,000 inner iterations, std::chrono::steady_clock, asm-volatile
// sink to defeat dead-store elimination.
//
// Measured median on the baseline host: 767.665 ns/call (range
// 756 .. 784 across the 10 reps). We commit the rounded median as a
// conservative number; the >=10x assertion has comfortable margin
// regardless of host jitter.
inline constexpr double V1_GET_HEADERS_NS_PER_CALL = 760.0;

}  // namespace httpserver::v1_baseline

#endif  // TEST_V1_BASELINE_V1_CONSTANTS_HPP_
