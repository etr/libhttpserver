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
// Two utilities are provided:
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

#ifndef TEST_BENCH_HARNESS_HPP_
#define TEST_BENCH_HARNESS_HPP_

#include <algorithm>
#include <chrono>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>  // _ReadWriteBarrier
#endif

#if defined(_MSC_VER)
// Single, ODR-safe sink for the MSVC do_not_optimize fallback (below). An
// `inline` variable gives exactly one definition across every TU that
// includes this header (C++17). do_not_optimize writes through it on each
// call, so the compiler must materialise the value being protected.
inline volatile const void* volatile do_not_optimize_sink = nullptr;
#endif

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
    // Unknown compiler: best-effort volatile-write fallback (still better
    // than a discarded local because the store target is volatile-qualified
    // at file scope and therefore observable).
    static volatile const void* volatile fallback_sink = nullptr;
    fallback_sink = static_cast<const void*>(&value);
    (void)fallback_sink;
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

#endif  // TEST_BENCH_HARNESS_HPP_
