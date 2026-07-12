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
// Shared microbench helpers. Included by every bench TU:
// bench_get_headers.cpp, bench_hook_overhead.cpp, bench_route_lookup.cpp,
// bench_warm_path.cpp, and (as an EXTRA_DIST documentation TU)
// measure_v1_get_headers.cpp.
//
// Until TASK-083 the hook/route/warm benches each carried a private
// duplicate of do_not_optimize, so the hardened MSVC sink could not reach
// them. They now all include this single canonical definition.
//
// The utilities provided:
//
//   do_not_optimize(value) — defeat dead-store elimination by feeding
//     the argument's address through an asm-volatile memory clobber.
//     We pass &value (a pointer) rather than value (the object) so that
//     the constraint is correct for non-trivial types like std::map and
//     header_view_map: the compiler sees an address, not a potentially
//     large object passed by value through an asm constraint.
//
//   run_bench_median(callable, outer, inner) — execute [callable] in
//     outer * inner calls, sort the per-outer-rep ns/call samples, and
//     return the median sample. [outer] should be odd so that
//     samples[outer/2] is an unambiguous middle element.
//
//   sort_and_median(v) / p99_of_sorted(v) — sample statistics shared by
//     the hook/route/warm benches (previously duplicated per-TU).
//
//   measure_median_ns(label, op, outer, inner[, warmup][, pre_round]) —
//     the full warmup + outer-rounds-of-inner-iterations timing loop
//     with median/p99/min/max reporting (previously triplicated per-TU).

#ifndef TEST_BENCH_HARNESS_HPP_
#define TEST_BENCH_HARNESS_HPP_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>

// Single guarded block: both the MSVC-only include and the ODR-safe sink
// declaration it supports (also shared by the unknown-compiler fallback in
// do_not_optimize below) are gated together here.
#if defined(_MSC_VER) || (!defined(__GNUC__) && !defined(__clang__))
#if defined(_MSC_VER)
#include <intrin.h>  // _ReadWriteBarrier
#endif

// Single, ODR-safe sink for the MSVC and unknown-compiler do_not_optimize
// fallbacks (below). An `inline` variable gives exactly one definition
// across every TU that includes this header (C++17). do_not_optimize writes
// through it on each call, so the compiler must materialise the value being
// protected.
// pointer is volatile so the store to it is an observable side effect;
// pointee is const void* to accept any address.
inline volatile const void* volatile do_not_optimize_sink = nullptr;
#endif

// TASK-083: sanitizer-build detection, shared by every bench TU that
// includes this header (previously copy-pasted verbatim in
// bench_hook_overhead.cpp, bench_route_lookup.cpp, and bench_warm_path.cpp).
// constexpr (without `inline`) gives each TU its own internal-linkage copy,
// so no ODR issue arises from the duplication across TUs.
constexpr bool kSanitizerBuild =
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

// ---------------------------------------------------------------------------
// do_not_optimize
// ---------------------------------------------------------------------------
// Prevent the compiler from eliding a call whose result is "not used".
// We feed the address of the value through an asm-volatile memory clobber
// ("r,m"(&value)) so the compiler must assume the pointed-to object may be
// read or written after the asm, keeping the call live.
//
// Note: we pass &value (pointer) rather than value (object), which is the
// correct form for non-trivial types — passing the object itself through an
// asm input constraint copies it by value into the constraint, which is
// undefined for non-trivially-copyable types. Passing the address is safe
// for any type.
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(&value) : "memory");
#elif defined(_MSC_VER)
    // Why the PREVIOUS MSVC sink was elidable: it was
    //     volatile const void* sink = static_cast<const void*>(&value);
    //     (void)sink;
    // i.e. a single volatile *read* of an address into a function-local
    // that nothing downstream observes. Under /O2 MSVC treats `sink` as a
    // dead local: the volatile qualifier sits on a pointer-to-const-void
    // whose value is never read after initialisation, so the whole store is
    // removed and `value` is no longer forced live across the call.
    //
    // The robust form composes two guarantees:
    //   1. _ReadWriteBarrier() — a documented MSVC compiler intrinsic that
    //      acts as a compile-time memory clobber: the optimiser may not
    //      reorder loads/stores across it. (Mirrors the `: "memory"` clobber
    //      in the gcc/clang asm-volatile form above.)
    //   2. A *write* through `do_not_optimize_sink`, a file-scope
    //      `volatile const void* volatile` pointer. A write to volatile-
    //      qualified storage is an observable side effect the compiler must
    //      emit; bracketing it with barriers pins &value live on both sides.
    _ReadWriteBarrier();
    do_not_optimize_sink = static_cast<const void*>(&value);
    _ReadWriteBarrier();
#else
    // Unknown compiler: best-effort volatile-write fallback through the
    // same ODR-safe inline sink used by the MSVC branch above (still
    // better than a discarded function-local static because the store
    // target is volatile-qualified at file scope and therefore observable,
    // with a single definition across every TU regardless of compiler).
    do_not_optimize_sink = static_cast<const void*>(&value);
#endif
}

// ---------------------------------------------------------------------------
// run_bench_median
// ---------------------------------------------------------------------------
// Run [callable] (a no-argument callable, typically a lambda) in a tight
// loop of [outer] repetitions x [inner] iterations each. Times each outer
// rep with std::chrono::steady_clock, computes ns/call per rep, sorts the
// samples, and returns the median sample.
//
// [outer] should be odd so that samples[outer/2] is the unambiguous middle
// element with no tie-breaking required.
template <typename F>
double run_bench_median(F callable, int outer, int inner) {
    using clock = std::chrono::steady_clock;
    std::vector<double> samples_ns;
    samples_ns.reserve(static_cast<std::size_t>(outer));
    for (int r = 0; r < outer; ++r) {
        auto t0 = clock::now();
        for (int i = 0; i < inner; ++i) {
            callable();
        }
        auto t1 = clock::now();
        const double ns_per_call =
            std::chrono::duration<double, std::nano>(t1 - t0).count() / inner;
        samples_ns.push_back(ns_per_call);
    }
    std::sort(samples_ns.begin(), samples_ns.end());
    return samples_ns[static_cast<std::size_t>(outer / 2)];
}

// ---------------------------------------------------------------------------
// sort_and_median / p99_of_sorted
// ---------------------------------------------------------------------------
// Sorts `v` in-place and returns the median. Callers MUST call this
// before p99_of_sorted because p99_of_sorted requires a pre-sorted
// vector (its precondition).
inline double sort_and_median(std::vector<double>& v) {
    // Precondition: v is non-empty (callers always pass outer>=1 samples).
    assert(!v.empty());
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

inline double p99_of_sorted(std::vector<double>& v) {
    // Precondition: v is sorted ascending (call sort_and_median first).
    const std::size_t idx = (v.size() * 99) / 100;
    return v[std::min(idx, v.size() - 1)];
}

// Default (no-op) pre-round callback for measure_median_ns below.
struct bench_no_pre_round {
    void operator()() const {}
};

// ---------------------------------------------------------------------------
// measure_median_ns
// ---------------------------------------------------------------------------
// Measure a no-arg callable's median ns/call over [outer] rounds of
// [inner] iterations each, after [warmup] untimed calls that prime the
// instruction cache and branch predictor (the true thermal warmup
// happens during the first outer round at INNER scale; the median over
// many rounds is robust to 1-2 high outlier early rounds).
//
// [pre_round] runs before the timed region of EVERY outer round
// (default: no-op). Benches that need a per-round cold start — e.g.
// bench_route_lookup's radix_pure, which invalidates the route cache —
// pass a callback here; its cost lands outside the timed window.
//
// Prints one summary line and returns the median:
//   label != nullptr:  "  <label>: median=...ns  p99=...ns  (min=... max=...)"
//   label == nullptr:  "    median=...ns  p99=...ns  (min=... max=...)"
// Keep these printf formats stable: CI may parse them.
//
// min_ns / max_ns are arrival-order extremes recorded BEFORE sorting
// (the cheapest and most expensive outer rounds in wall-clock order,
// useful for spotting warm-up outliers).
template <typename F, typename PreRound = bench_no_pre_round>
double measure_median_ns(const char* label, F op,
                         std::size_t outer, std::size_t inner,
                         std::size_t warmup = 10'000,
                         PreRound pre_round = PreRound{}) {
    using clock = std::chrono::steady_clock;
    std::vector<double> samples_ns;
    samples_ns.reserve(outer);

    for (std::size_t i = 0; i < warmup; ++i) {
        op();
    }

    for (std::size_t r = 0; r < outer; ++r) {
        pre_round();
        const auto t0 = clock::now();
        for (std::size_t i = 0; i < inner; ++i) {
            op();
        }
        const auto t1 = clock::now();
        const double ns_per_call =
            std::chrono::duration<double, std::nano>(t1 - t0).count() / inner;
        samples_ns.push_back(ns_per_call);
    }

    const double min_ns =
        *std::min_element(samples_ns.begin(), samples_ns.end());
    const double max_ns =
        *std::max_element(samples_ns.begin(), samples_ns.end());
    // sort_and_median sorts samples_ns in-place; p99_of_sorted relies on
    // this sorted state (precondition), so this call order is mandatory.
    const double median = sort_and_median(samples_ns);
    const double p99 = p99_of_sorted(samples_ns);
    if (label != nullptr) {
        std::printf(
            "  %s: median=%.3fns  p99=%.3fns  (min=%.3fns max=%.3fns)\n",
            label, median, p99, min_ns, max_ns);
    } else {
        std::printf("    median=%.3fns  p99=%.3fns  (min=%.3fns max=%.3fns)\n",
                    median, p99, min_ns, max_ns);
    }
    return median;
}

#endif  // TEST_BENCH_HARNESS_HPP_
