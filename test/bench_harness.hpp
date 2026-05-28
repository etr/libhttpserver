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
// Shared microbench helpers used by bench_get_headers.cpp and
// (as an EXTRA_DIST documentation TU) measure_v1_get_headers.cpp.
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
//
// MSVC fallback: volatile-pointer write acts as an optimisation barrier.
// This may be elided by aggressive optimisers; see bench documentation for
// the known limitation.
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(&value) : "memory");
#else
    // MSVC fallback: take address via volatile sink.
    // Limitation: aggressive MSVC optimisers may still elide this on
    // newer standards (/O2 /std:c++20). For a more robust MSVC sink,
    // consider _ReadWriteBarrier() or __iso_volatile_store64.
    volatile const void* sink = static_cast<const void*>(&value);
    (void)sink;
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
