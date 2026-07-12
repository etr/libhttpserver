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
// TASK-058 -- Warm-path allocation pass benchmark.
//
// Six measurements isolate the per-request allocations that TASK-058
// (and TASK-072, variants 5 and 6) target:
//   (1) canonicalize: lookup_v2() on a canonical path.  Pre-TASK-058 this
//       allocated a std::string in canonicalize_lookup_path on every
//       call; after step 1 the happy path returns a string_view into
//       the caller's argument and allocates nothing.
//   (2) should_skip_auth (non-empty list): drives the per-request
//       normalize_path call when the skip-paths list is non-empty.
//       After step 2 the list is pre-normalized at construction so the
//       per-call cost drops to just the request-side normalize and a
//       linear scan over (already-canonical) entries.
//   (3) should_skip_auth (empty list): pre-TASK-058 still normalized
//       the request path even when no skip paths were configured.
//       After step 2 the empty-list early-out short-circuits before
//       the normalize call.
//   (4) serialize_allow_405: the cost of building the Allow: header
//       value for a 405 response.  Pre-TASK-058 this rebuilds the
//       string on every 405; after step 3 a per-resource cache returns
//       the previously-computed std::string by reference.
//   (5) build_request_args_pct2f: TASK-072 -- the cost of inserting a
//       GET arg whose value contains "%2F" (the canonical percent-
//       encoded form encountered in real workloads).  Before TASK-072
//       this allocated a std::string temporary on the global heap;
//       after TASK-072 the unescape output is materialised directly
//       in the per-connection arena.
//   (6) build_request_args_plain: TASK-072 baseline -- same operation
//       but with a value that has no percent-encoded sequences.  The
//       median for (5) should land within noise of the (6) baseline.
//
// Wired into `make bench` via bench_targets in test/Makefile.am; not
// part of `make check`.  Sanitizer builds skip with exit 0 so the
// bench stays green on sanitizer hosts (same convention as
// bench_route_lookup).
//
// CI gate (TASK-083): every measurement is checked against a committed
// per-platform baseline in bench_baseline.hpp.  The bench fails (rc=1)
// when any median regresses more than 5% over its baseline -- i.e. the
// warm path must stay within 5% of the committed numbers (the
// ">= 5% improvement vs baseline" acceptance from TASK-058, expressed
// here as fail-on-regression).  Refresh the baselines via TASK-084 when
// the runner hardware changes.

#define HTTPSERVER_COMPILATION 1  // unlock webserver_test_access

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

#include "httpserver/create_webserver.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
#include "httpserver/webserver.hpp"
#include "httpserver/detail/http_request_impl.hpp"  // TASK-072: build_request_args bench
#include "httpserver/detail/webserver_impl.hpp"
#include "bench_baseline.hpp"  // NOLINT(build/include_subdir)
#include "bench_harness.hpp"   // NOLINT(build/include_subdir) -- do_not_optimize, measure_median_ns

namespace hs = httpserver;

namespace {

class noop_resource : public hs::http_resource {
 public:
    hs::http_response render_get(const hs::http_request&) override {
        return hs::http_response::string("ok");
    }
};

// Build a webserver with auth_handler_paths configured so the
// auth-skip path is exercised.  We never start the daemon (no MHD,
// no socket).
std::unique_ptr<hs::webserver> make_bench_webserver(
        std::vector<std::string> skip_paths) {
    // A no-op auth_handler so the auth surface is engaged.
    auto auth = [](const hs::http_request&)
        -> std::optional<hs::http_response> { return std::nullopt; };
    auto ws = std::make_unique<hs::webserver>(
        hs::create_webserver(8080)
            .start_method(hs::http::http_utils::INTERNAL_SELECT)
            .auth_handler(auth)
            .auth_skip_paths(std::move(skip_paths)));
    ws->register_path("/api/v1/users/me", std::make_shared<noop_resource>());
    // Resource with restricted method set so 405 path exercises
    // serialize_allow_methods.  Only GET is allowed.
    auto restricted = std::make_shared<noop_resource>();
    restricted->disallow_all();
    restricted->set_allowing(hs::http_method::get, true);
    ws->register_path("/api/v1/restricted", restricted);
    return ws;
}

// OUTER=51 matches bench_hook_overhead and bench_route_lookup: with 51
// rounds the median is the 26th sorted value (exact midpoint), so 1-2
// high outlier rounds from OS scheduling noise cannot shift the median.
// With OUTER=11 (the old value) the median was the 6th value and a
// single outlier round could flip a <10 ns gate; on a shared CI runner
// this produced spurious failures independent of real regressions.
// Wall-clock cost: 51 rounds * 1M inner at ~12 ns/call ≈ 612 ms total,
// acceptable for an out-of-band `make bench` run.
//
// INNER_405=100K: serialize_allow_405 is more expensive than the other
// measurements (~40 ns), so 100K inner (4 ms of signal per outer round)
// keeps wall time bounded. The 51-outer median is still stable at this
// inner count because we care about median ns/call, not absolute signal.
constexpr std::size_t OUTER = 51;
constexpr std::size_t INNER = 1'000'000;
constexpr std::size_t INNER_405 = 100'000;

// TASK-072: shared setup for bench sections (5) and (6) in main() below.
// Both insert one GET arg into a fresh arena-backed http_request_impl and
// differ only in whether `value` contains a %2F escape sequence (and
// therefore in the printed `label`). Extracting the common buf/arena/impl/
// accumulator plumbing keeps that setup in one place instead of duplicated
// per section.
//
// Each measured call mirrors the full production request lifecycle:
// allocate a fresh arena-backed impl, insert one arg (the warm path the
// task targets), and destroy the impl.  The 65536-byte arena is more than
// large enough for a single insert, so all pmr::string allocations stay
// inside the arena and zero global-heap allocation occurs during the
// measured window.
//
// Prior design flaw (TASK-083): the old loop accumulated all 1M values
// under a single key without resetting the arena.  The arena was
// exhausted after ~819 iterations (~65536 / 80 bytes per pmr::string),
// so every subsequent call spilled to the upstream heap -- measuring
// exactly the allocation overhead the task was supposed to eliminate.
// The current design (fresh monotonic_buffer_resource per call) removes
// this flaw and makes the bench an honest proof of the zero-heap-alloc
// guarantee.
//
// buf is declared OUTSIDE the lambda: zero-initialising a 65536-byte stack
// array inside the inner loop would touch 64 KiB of cache traffic per
// iteration (64 GB/round), swamping the measured pmr/string cost.  Instead,
// buf is zeroed ONCE before the timed loop and the lambda constructs a
// fresh monotonic_buffer_resource from the same backing storage on every
// call (cheap: just sets two internal pointers), which resets the
// bump-pointer without re-zeroing the memory.  The arena writes over
// stale bytes from the previous call, which is correct.
double run_build_args_bench(const char* label, const char* value) {
    using httpserver::detail::http_request_impl;
    using httpserver::detail::arguments_accumulator;
    using impl_alloc_t =
        std::pmr::polymorphic_allocator<http_request_impl>;

    // Backing buffer zeroed once; reused across all inner iterations
    // by constructing a fresh monotonic_buffer_resource each call.
    alignas(std::max_align_t) std::array<std::byte, 65536> buf{};
    // defence-in-depth: ensures the compiler treats buf as potentially
    // read by the monotonic_buffer_resource.
    do_not_optimize(buf);

    return measure_median_ns(
        label,
        [&]() {
            // Fresh monotonic_buffer_resource resets the bump pointer
            // to buf.data() without touching the bytes -- cheap.
            std::pmr::monotonic_buffer_resource arena(
                buf.data(), buf.size(), std::pmr::null_memory_resource());
            impl_alloc_t alloc(&arena);
            auto* p = alloc.new_object<http_request_impl>(
                nullptr, nullptr, alloc);
            arguments_accumulator aa;
            aa.unescaper = nullptr;
            aa.arguments = &p->unescaped_args;
            aa.max_args_count = 64;
            aa.max_args_bytes = 64 * 128;
            http_request_impl::build_request_args(
                &aa, MHD_GET_ARGUMENT_KIND, "k", value);
            do_not_optimize(p->unescaped_args);
            alloc.delete_object(p);
        },
        OUTER, INNER);
}

}  // namespace

int main() {
    if constexpr (kSanitizerBuild) {
        std::printf("bench_warm_path: skipped (sanitizer build "
                    "would distort ns/call)\n");
        return 0;
    }

    // Medians for the six measurements, lifted out of their per-scope
    // blocks so the gate at the end of main() can compare each against
    // its committed baseline.
    double med_canonicalize = 0.0;
    double med_skip_auth_nonempty = 0.0;
    double med_skip_auth_empty = 0.0;
    double med_serialize_allow_405 = 0.0;
    double med_build_args_pct2f = 0.0;
    double med_build_args_plain = 0.0;

    // ----- (1) canonicalize: lookup_v2 on a canonical path. -----
    {
        auto ws = make_bench_webserver({"/public", "/health"});
        auto* impl = hs::webserver_test_access::impl(*ws);
        static const std::string kPath("/api/v1/users/me");
        // Warm the cache: subsequent lookups hit the cache tier, so
        // canonicalize_lookup_path is what dominates the visible work.
        {
            auto warm = impl->lookup_v2(hs::http_method::get, kPath);
            do_not_optimize(warm);
        }
        std::printf("bench_warm_path (1): canonicalize "
                    "(/api/v1/users/me cache-hit)\n");
        med_canonicalize = measure_median_ns(
            "canonicalize",
            [&]() {
                auto r = impl->lookup_v2(hs::http_method::get, kPath);
                do_not_optimize(r);
            },
            OUTER, INNER);
    }

    // ----- (2) should_skip_auth on a non-empty list. -----
    {
        auto ws = make_bench_webserver({"/public", "/health"});
        auto* impl = hs::webserver_test_access::impl(*ws);
        static const std::string kPath("/api/v1/users/me");
        std::printf("bench_warm_path (2): should_skip_auth "
                    "(non-empty skip list)\n");
        med_skip_auth_nonempty = measure_median_ns(
            "should_skip_auth_nonempty",
            [&]() {
                bool r = impl->should_skip_auth(kPath);
                do_not_optimize(r);
            },
            OUTER, INNER);
    }

    // ----- (3) should_skip_auth on an empty list (the common case
    // for servers with no auth_handler configured -- but the bench
    // still installs an auth_handler so the dispatch surface engages
    // it).  This is the production-typical case for servers that
    // either don't configure skip paths at all, or configure
    // auth_skip_paths({}) explicitly. -----
    {
        auto ws = make_bench_webserver({});
        auto* impl = hs::webserver_test_access::impl(*ws);
        static const std::string kPath("/api/v1/users/me");
        std::printf("bench_warm_path (3): should_skip_auth "
                    "(empty skip list)\n");
        med_skip_auth_empty = measure_median_ns(
            "should_skip_auth_empty",
            [&]() {
                bool r = impl->should_skip_auth(kPath);
                do_not_optimize(r);
            },
            OUTER, INNER);
    }

    // ----- (4) serialize_allow_405: cost of building the Allow:
    // header value on a 405.  Drive serialize_allow_methods directly
    // to isolate the format cost without spinning up MHD. -----
    {
        auto ws = make_bench_webserver({});
        auto* impl = hs::webserver_test_access::impl(*ws);
        // Build a fresh resource with a non-trivial mask.
        noop_resource r;
        r.disallow_all();
        r.set_allowing(hs::http_method::get, true);
        r.set_allowing(hs::http_method::head, true);
        r.set_allowing(hs::http_method::post, true);
        std::printf("bench_warm_path (4): serialize_allow_405 "
                    "(GET, HEAD, POST mask)\n");
        med_serialize_allow_405 = measure_median_ns(
            "serialize_allow_405",
            [&]() {
                std::string s = impl->serialize_allow_methods(
                    r.get_allowed_methods());
                do_not_optimize(s);
            },
            OUTER, INNER_405);
    }

    // ----- (5) TASK-072: build_request_args with a %2F-containing value,
    // one insert per fresh per-request arena. See run_build_args_bench
    // above for the shared setup and the reasoning behind it. -----
    {
        // Long enough to exceed std::string SSO on libc++ and libstdc++.
        // Contains "%2F" so the default unescape path does real work.
        static const char* kValuePct2f =
            "a%2Fbcdefghijklmnopqrstuvwxyz_padding_to_force_heap";

        std::printf("bench_warm_path (5): build_request_args "
                    "(%%2F unescape via arena, fresh impl per call)\n");
        med_build_args_pct2f =
            run_build_args_bench("build_request_args_pct2f", kValuePct2f);
    }

    // ----- (6) TASK-072: baseline with no percent-encoded sequences.
    // Same fresh-impl-per-call structure as (5) so the timings are
    // directly comparable.  The median for (5) should land within noise
    // of this baseline once TASK-072 lands. -----
    {
        static const char* kValuePlain =
            "abcdefghijklmnopqrstuvwxyz_no_escape_baseline_padding";

        std::printf("bench_warm_path (6): build_request_args "
                    "(no-escape baseline, fresh impl per call)\n");
        med_build_args_plain =
            run_build_args_bench("build_request_args_plain", kValuePlain);
    }

#if !defined(__APPLE__)
    // TASK-083/TASK-084: the baselines below for this platform are
    // conservative, uncalibrated TODO(TASK-084) placeholders (~3x the
    // apple-silicon medians, see bench_baseline.hpp) -- surface that in
    // the bench output/CI logs so the reduced gating confidence is visible
    // even when nobody is reading bench_baseline.hpp's comments.
    std::printf("\nNOTE: warm-path baselines on this platform are "
                "uncalibrated TODO(TASK-084) placeholders (~3x "
                "apple-silicon medians); the regression gate below has "
                "reduced confidence until TASK-084 calibrates them.\n");
#endif

    // ----- Summary + gates (TASK-083) -----
    // Each median is compared against its committed per-platform baseline
    // (bench_baseline.hpp). A median more than kAllowedRegressionRatio
    // (5%) over baseline fails the bench.
    //
    // Noise floor: for sub-10 ns measurements (e.g. WARM_SHOULD_SKIP_AUTH_EMPTY_NS
    // baseline = 2 ns) a pure 5% ratio produces an allowed window of only 0.1 ns,
    // which is below steady_clock resolution on many platforms. We add an absolute
    // additive floor so the gate ceiling is at least baseline + kAbsoluteNoiseFloorNs
    // regardless of the ratio, matching the pattern used in bench_hook_overhead.
    constexpr double kAbsoluteNoiseFloorNs = 5.0;

    namespace bb = httpserver::bench_baseline;
    std::printf("\nbench_warm_path summary (baselines from "
                "bench_baseline.hpp, +%.0f%% or +%.0f ns floor allowed):\n",
                100.0 * (bb::kAllowedRegressionRatio - 1.0),
                kAbsoluteNoiseFloorNs);

    int rc = 0;
    const auto check = [&](const char* label, double measured,
                           double baseline) {
        // Use the larger of the ratio-based ceiling and the absolute noise
        // floor so sub-ns timer quantization cannot false-trip the gate.
        const double ratio_ceiling  = baseline * bb::kAllowedRegressionRatio;
        const double floor_ceiling  = baseline + kAbsoluteNoiseFloorNs;
        const double allowed = std::max(ratio_ceiling, floor_ceiling);
        const double pct = 100.0 * (measured / baseline - 1.0);
        std::printf("  %-26s median=%8.3f ns  baseline=%8.3f ns  %+6.1f%%\n",
                    label, measured, baseline, pct);
        if (measured > allowed) {
            std::printf("FAIL: %s median %.3f ns exceeds gate ceiling %.3f ns "
                        "(baseline*%.2f=%.3f, baseline+%.0fns=%.3f, "
                        "regression %+.1f%%)\n",
                        label, measured, allowed,
                        bb::kAllowedRegressionRatio, ratio_ceiling,
                        kAbsoluteNoiseFloorNs, floor_ceiling, pct);
            rc = 1;
        }
    };
    check("canonicalize", med_canonicalize, bb::WARM_CANONICALIZE_NS);
    check("should_skip_auth_nonempty", med_skip_auth_nonempty,
          bb::WARM_SHOULD_SKIP_AUTH_NONEMPTY_NS);
    check("should_skip_auth_empty", med_skip_auth_empty,
          bb::WARM_SHOULD_SKIP_AUTH_EMPTY_NS);
    check("serialize_allow_405", med_serialize_allow_405,
          bb::WARM_SERIALIZE_ALLOW_405_NS);
    check("build_request_args_pct2f", med_build_args_pct2f,
          bb::WARM_BUILD_REQUEST_ARGS_PCT2F_NS);
    check("build_request_args_plain", med_build_args_plain,
          bb::WARM_BUILD_REQUEST_ARGS_PLAIN_NS);

    if (rc == 0) {
        std::printf("PASS: all warm-path medians within gate "
                    "(+%.0f%% or +%.0fns floor over baseline)\n",
                    100.0 * (bb::kAllowedRegressionRatio - 1.0),
                    kAbsoluteNoiseFloorNs);
    }
    return rc;
}
