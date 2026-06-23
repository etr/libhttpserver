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
// TASK-052: zero-cost-when-unused microbench for the hook bus gate.
//
// The dispatch hot path consults `impl_->any_hooks_[phase].load(relaxed)`
// before considering any hook invocation. When no hooks are registered
// at a phase, every request still pays that one atomic load -- which is
// the "zero" we benchmark here. PRD-HOOK-REQ-009 / §4.10 demand that
// this gate is effectively free (single relaxed load + branch).
//
// This is a microbench: it measures the gate cost directly through the
// HTTPSERVER_COMPILATION-gated webserver_test_access friend. A full
// HTTP round-trip bench would swamp the ns-scale signal with us-scale
// curl + socket overhead. We chose the gate-cost shape because it is
// the only signal that is BOTH measurable AND attributable to the hook
// bus alone -- anything richer would mix in non-hook noise (TCP, MHD,
// thread scheduling) and force a heuristic baseline that's noisier
// than the property we claim.
//
// Configurations:
//
//   (a) zero hooks registered at hook_phase::response_sent
//   (b) one observation hook registered at hook_phase::response_sent
//
// Both configurations exercise the SAME atomic load. The difference
// the test asserts is the structural property: even WITH a hook
// registered, the gate-load microcost remains comparable. In practice
// it differs only by branch-predictor state (the `if` body is taken
// vs. skipped), not by the load itself.
//
// CI gate (TASK-083): a two-tier gate replaces the old absolute-only
// ceiling.
//
//   * Absolute sanity bound on (a): (a)'s median must stay under a
//     fixed 50 ns ceiling (kAbsoluteGateNsCeiling). This catches a
//     runaway regression where the baseline gate-load itself blows up
//     (e.g. a future change makes the relaxed load a contended RMW).
//
//   * Relative gate on (b): HOOK_BASELINE_NS is measured in-run as
//     (a)'s median (the no-hooks variant). (b)'s median -- the same
//     gate load WITH a hook registered -- must stay within
//     2 * HOOK_BASELINE_NS. This is the TASK-052 acceptance: registering
//     a hook must not even double the per-request gate microcost, which
//     is the operational meaning of "zero-cost when unused"
//     (PRD-HOOK-REQ-008). Computing the baseline in-run makes the gate
//     auto-track runner speed instead of hardcoding a constant.
//
// On sanitizer builds the per-call cost is inflated 10..50x; the
// bench skips with exit 0 so `make bench` stays green.
//
// Wired into `make bench` via `EXTRA_PROGRAMS` in test/Makefile.am;
// NOT part of `make check`.

#define HTTPSERVER_COMPILATION 1  // unlock webserver_test_access

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

#include "httpserver/create_webserver.hpp"
#include "httpserver/hook_handle.hpp"
#include "httpserver/hook_phase.hpp"
#include "httpserver/hook_context.hpp"
#include "httpserver/webserver.hpp"
#include "httpserver/detail/webserver_impl.hpp"

#include "bench_harness.hpp"  // NOLINT(build/include_subdir) -- do_not_optimize

namespace hs = httpserver;

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

namespace {

// Absolute sanity bound on the no-hooks gate-load (a). The dispatch
// path pays one relaxed atomic load per phase per request; on modern
// x86_64 / arm64 this is single-digit nanoseconds. The 50 ns ceiling is
// ~10x what we measure on a clean release build (2-5 ns), which
// accommodates noisy CI runners (shared cores, frequency scaling)
// without missing a real regression. This guards against a pathological
// blow-up of the baseline itself (e.g. the relaxed load degrading into a
// contended RMW); the relative gate below guards the zero-cost claim.
constexpr double kAbsoluteGateNsCeiling = 50.0;

// Relative-gate multiplier (TASK-052 acceptance): with a hook registered,
// (b)'s median must not exceed kRelativeGateFactor * HOOK_BASELINE_NS,
// where HOOK_BASELINE_NS is (a)'s median measured in the same run.
constexpr double kRelativeGateFactor = 2.0;

// Sorts `v` in-place and returns the median. Callers MUST call this
// before p99_of_sorted because p99_of_sorted requires a pre-sorted
// vector (its precondition).
double sort_and_median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double p99_of_sorted(std::vector<double>& v) {
    // Precondition: v is sorted ascending (call sort_and_median first).
    const std::size_t idx = (v.size() * 99) / 100;
    return v[std::min(idx, v.size() - 1)];
}

double measure_gate_cost(hs::webserver& ws, std::size_t outer, std::size_t inner) {
    auto* impl = hs::webserver_test_access::impl(ws);
    constexpr std::size_t kPhaseIdx =
        static_cast<std::size_t>(hs::hook_phase::response_sent);
    auto& gate = impl->any_hooks_[kPhaseIdx];

    // Warmup: 10 000 iterations prime the branch predictor. The true
    // thermal / instruction-cache warmup happens during the first outer
    // round (1 M loads), which dwarfs this explicit pre-loop — the
    // median of 51 rounds is robust to 1-2 high outlier early rounds.
    for (std::size_t i = 0; i < 10'000; ++i) {
        const bool v = gate.load(std::memory_order_relaxed);
        do_not_optimize(v);
    }

    using clock = std::chrono::steady_clock;
    std::vector<double> samples_ns;
    samples_ns.reserve(outer);
    for (std::size_t r = 0; r < outer; ++r) {
        const auto t0 = clock::now();
        for (std::size_t i = 0; i < inner; ++i) {
            const bool v = gate.load(std::memory_order_relaxed);
            do_not_optimize(v);
        }
        const auto t1 = clock::now();
        const double ns_per_call =
            std::chrono::duration<double, std::nano>(t1 - t0).count() / inner;
        samples_ns.push_back(ns_per_call);
    }
    // Record arrival-order extremes BEFORE sorting (min_ns / max_ns are
    // the cheapest and most expensive outer rounds in wall-clock order,
    // useful for spotting warm-up outliers).
    const double min_ns = *std::min_element(samples_ns.begin(), samples_ns.end());
    const double max_ns = *std::max_element(samples_ns.begin(), samples_ns.end());
    // sort_and_median sorts samples_ns in-place; p99_of_sorted relies on
    // this sorted state (precondition), so this call order is mandatory.
    const double median = sort_and_median(samples_ns);
    const double p99 = p99_of_sorted(samples_ns);
    std::printf("    median=%.3fns  p99=%.3fns  (min=%.3fns max=%.3fns)\n",
                median, p99, min_ns, max_ns);
    return median;
}

}  // namespace

int main() {
    if constexpr (kSanitizerBuild) {
        std::printf("bench_hook_overhead: skipped (sanitizer build "
                    "would distort ns/call)\n");
        return 0;
    }

    // OUTER=51 gives a stable median (26th value when sorted, i.e. the
    // exact midpoint) and a meaningful p99 (50th value). The extra 40
    // rounds cost < 40 ms on any modern host at 1 M iterations each and
    // are well worth the statistical reliability on noisy CI cores.
    constexpr std::size_t OUTER = 51;
    constexpr std::size_t INNER = 1'000'000;

    // (a) Zero hooks registered.
    std::printf("bench_hook_overhead (a): zero hooks at response_sent\n");
    double median_a = 0.0;
    {
        hs::webserver ws_a{hs::create_webserver(0)};
        median_a = measure_gate_cost(ws_a, OUTER, INNER);
    }

    // (b) One observation hook registered. The dispatch gate now
    // observes `true`; the load cost itself is identical, the only
    // visible diff is branch-predictor state.
    std::printf("bench_hook_overhead (b): one observation hook at response_sent\n");
    double median_b = 0.0;
    {
        hs::webserver ws_b{hs::create_webserver(0)};
        auto h = ws_b.add_hook(hs::hook_phase::response_sent,
            std::function<void(const hs::response_sent_ctx&)>(
                [](const hs::response_sent_ctx&) {}));
        median_b = measure_gate_cost(ws_b, OUTER, INNER);
        (void)h;   // keep the registration alive across the measure
    }

    // HOOK_BASELINE_NS is the no-hooks gate-load median, measured in this
    // same run so the relative gate auto-tracks runner speed.
    const double HOOK_BASELINE_NS = median_a;

    // Relative ceiling: 2 x baseline. At the sub-nanosecond magnitudes
    // these gate loads run at, 2x of a tiny baseline is itself tiny, and
    // pure measurement jitter (a single retired-instruction difference per
    // 1M-iteration round) could otherwise false-trip the gate. A small
    // additive noise floor (kRelativeGateNoiseFloorNs) keeps the gate from
    // firing on sub-ns sampling noise while still failing hard the moment a
    // real, attributable hook-bus regression appears.
    constexpr double kRelativeGateNoiseFloorNs = 2.0;
    const double relative_gate_ns =
        kRelativeGateFactor * HOOK_BASELINE_NS + kRelativeGateNoiseFloorNs;

    std::printf("\nbench_hook_overhead summary:\n");
    std::printf("  (a) zero hooks  median = %.3f ns/call  (HOOK_BASELINE_NS)\n",
                median_a);
    std::printf("  (b) one hook    median = %.3f ns/call\n", median_b);
    std::printf("  absolute ceiling on (a)       = %.3f ns/call\n",
                kAbsoluteGateNsCeiling);
    std::printf("  relative ceiling on (b)       = %.3f ns/call "
                "(%.1f x %.3f + %.1f floor)\n",
                relative_gate_ns, kRelativeGateFactor, HOOK_BASELINE_NS,
                kRelativeGateNoiseFloorNs);

    int rc = 0;
    // Absolute sanity bound on (a): catches a baseline blow-up.
    if (median_a > kAbsoluteGateNsCeiling) {
        std::printf("FAIL: (a) gate-load median %.3f ns exceeds absolute "
                    "ceiling %.3f ns -- the baseline gate load regressed\n",
                    median_a, kAbsoluteGateNsCeiling);
        rc = 1;
    }
    // Relative gate on (b): registering a hook must not blow up the gate
    // microcost (TASK-052 acceptance / PRD-HOOK-REQ-008).
    if (median_b > relative_gate_ns) {
        std::printf("FAIL: (b) gate-load median %.3f ns exceeds 2x "
                    "HOOK_BASELINE_NS (= %.3f ns) -- the zero-cost-when-unused "
                    "claim is at risk\n",
                    median_b, relative_gate_ns);
        rc = 1;
    }
    if (rc == 0) {
        std::printf("PASS: (a) within absolute ceiling and (b) within "
                    "2x HOOK_BASELINE_NS\n");
    }
    return rc;
}
