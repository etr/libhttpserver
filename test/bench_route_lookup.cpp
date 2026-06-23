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
// TASK-053 -- v2 dispatch performance acceptance harness.
//
// After step 3 of TASK-053 cut `resolve_resource_for_request` over to
// `lookup_v2()` and removed the v1 fallback, the dispatch hot path is
// the cache -> exact -> radix -> regex pipeline plus the per-call LRU
// promotion. The deferred-backlog plan (TASK-053 §"Step 4 -- Bench")
// fixes two ceilings on that pipeline:
//
//   (a) cache_warm_ns ceiling  -- 200 ns / lookup (median, cache hit)
//   (b) radix_pure_ns ceiling  -- 5 us / lookup for 8-segment paths
//                                 (median, cache cold -- radix only)
//
// TASK-083 separates the two measurements cleanly. Previously (b)
// rotated through only 16 paths, all of which fit inside the 256-entry
// LRU after the first round, so (b) actually measured a cache-warm +
// radix MIX rather than pure radix-tier latency. The new (b) drives a
// path set larger than the LRU capacity AND invalidates the cache
// before every outer round, so the inner loop misses the cache on every
// iteration and the measured cost is the trie walk alone.
//
// The ceilings are generous (~5-10x the cost on modern x86_64 / arm64
// release builds) to absorb CI runner noise, frequency scaling and
// virtualized hosts without missing a genuine regression. The point
// is to lock in *order of magnitude*: the v1 dispatch path was
// dominated by std::regex_match across the full regex map at every
// miss; v2 is dominated by a single std::unordered_map probe at the
// cache tier and a per-segment trie walk at the radix tier. If a
// future change reintroduces O(N) regex scanning on the cache-hit
// path, (a)'s ceiling catches it; if a future change makes the radix
// walk allocate-per-segment, (b)'s ceiling catches it.
//
// Wired into `make bench` via `bench_targets` in test/Makefile.am;
// NOT part of `make check`. Sanitizer builds skip with exit 0 so
// `make bench` stays green on sanitizer hosts.

#define HTTPSERVER_COMPILATION 1  // unlock webserver_test_access

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "httpserver/create_webserver.hpp"
#include "httpserver/http_resource.hpp"
#include "httpserver/http_response.hpp"
#include "httpserver/http_utils.hpp"
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

// Ceilings from the TASK-053 deferred-backlog plan, step 4.
constexpr double kCacheHitNsCeiling = 200.0;     // ns/lookup, median
constexpr double kRadixUsCeiling    = 5.0;       // us/lookup, median
constexpr double kRadixNsCeiling    = kRadixUsCeiling * 1000.0;

class noop_resource : public hs::http_resource {
 public:
    hs::http_response render_get(const hs::http_request&) override {
        return hs::http_response::string("ok");
    }
};

double sort_and_median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double p99_of_sorted(std::vector<double>& v) {
    // Precondition: v is sorted ascending (call sort_and_median first).
    const std::size_t idx = (v.size() * 99) / 100;
    return v[std::min(idx, v.size() - 1)];
}

// Measure a no-arg lambda's median ns/call over OUTER rounds of
// INNER iterations each. Prints a one-line summary.
template <typename F>
double measure_median_ns(const char* label, F op,
                         std::size_t outer, std::size_t inner) {
    using clock = std::chrono::steady_clock;
    std::vector<double> samples_ns;
    samples_ns.reserve(outer);

    // Warmup: prime instruction cache + branch predictor. The first
    // outer round handles thermal warmup at INNER scale; this short
    // pre-loop just gets the trampoline hot.
    for (std::size_t i = 0; i < 10'000; ++i) {
        op();
    }

    for (std::size_t r = 0; r < outer; ++r) {
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
    const double median = sort_and_median(samples_ns);
    const double p99 = p99_of_sorted(samples_ns);
    std::printf("  %s: median=%.3fns  p99=%.3fns  (min=%.3fns max=%.3fns)\n",
                label, median, p99, min_ns, max_ns);
    return median;
}

// Build a webserver with the bench routes registered. We never start
// the daemon (no MHD, no socket) -- the bench exercises only the
// route-table data path through webserver_impl::lookup_v2().
std::unique_ptr<hs::webserver> make_bench_webserver() {
    auto ws = std::make_unique<hs::webserver>(
        hs::create_webserver(8080)
            .start_method(hs::http::http_utils::INTERNAL_SELECT));
    // (a) cache-hit target: a single exact path repeatedly looked up.
    ws->register_path("/api/v1/users/me", std::make_shared<noop_resource>());
    // (b) radix-tier target: an 8-segment parameterized path. Each
    // segment is a wildcard so the lookup walks all 8 levels of the
    // trie under wildcard children.
    ws->register_path(
        "/a/{p1}/b/{p2}/c/{p3}/d/{p4}",
        std::make_shared<noop_resource>());
    return ws;
}

// Generate `count` distinct 8-segment query paths that all match the
// registered template "/a/{p1}/b/{p2}/c/{p3}/d/{p4}". Each fills the four
// wildcard slots with a per-path unique token so consecutive lookups never
// resolve to the same cache key. `count` must exceed the LRU capacity so
// that, once invalidated, the inner loop misses on every iteration.
std::vector<std::string> make_radix_paths(std::size_t count) {
    std::vector<std::string> paths;
    paths.reserve(count);
    char buf[64];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(buf, sizeof(buf),
                      "/a/u%04zu/b/v%04zu/c/w%04zu/d/x%04zu",
                      i, i, i, i);
        paths.emplace_back(buf);
    }
    return paths;
}

}  // namespace

int main() {
    if constexpr (kSanitizerBuild) {
        std::printf("bench_route_lookup: skipped (sanitizer build "
                    "would distort ns/call)\n");
        return 0;
    }

    // OUTER=51 gives a stable median (26th value when sorted) and a
    // meaningful p99 (50th value). The outer rounds cost < 100 ms
    // total at the inner counts below.
    constexpr std::size_t OUTER = 51;
    constexpr std::size_t INNER_CACHE = 1'000'000;   // cache hit is ~ns
    constexpr std::size_t INNER_RADIX = 100'000;     // radix walk is ~us

    auto ws = make_bench_webserver();
    auto* impl = hs::webserver_test_access::impl(*ws);

    // ----- (a) cache-hit -----
    // Pre-allocate the query path outside the timed region so the bench
    // measures only the lookup machinery, not std::string construction.
    static const std::string kCacheHitPath("/api/v1/users/me");

    // First call warms the cache. Subsequent calls hit it.
    {
        auto warm = impl->lookup_v2(hs::http_method::get, kCacheHitPath);
        do_not_optimize(warm);
    }

    std::printf("bench_route_lookup (a): cache_warm (/api/v1/users/me)\n");
    const double median_cache_warm_ns = measure_median_ns(
        "cache_warm",
        [&]() {
            auto r = impl->lookup_v2(hs::http_method::get, kCacheHitPath);
            do_not_optimize(r);
        },
        OUTER, INNER_CACHE);

    // ----- (b) radix_pure tier, 8 segments, cache COLD -----
    // To measure the trie walk in isolation (not a cache-warm mix) we
    // (1) rotate through more distinct paths than the LRU can hold and
    // (2) invalidate the cache before every outer round so the inner
    // loop's first iterations are guaranteed misses. With kNumPaths >
    // ROUTE_CACHE_MAX_SIZE (256) the LRU evicts on every iteration past
    // its capacity, so the steady-state hit rate is effectively zero and
    // each lookup pays the full radix walk.
    //
    // Note (performance-reviewer-iter1-1): during the first ≤256 iterations
    // of each outer round, the LRU progressively re-warms from the
    // invalidated state. These iterations see a mix of cold-miss and
    // warming costs rather than pure radix latency. However, with
    // INNER_RADIX=100K this warm-up window is only ~0.25% of inner
    // iterations per round (256 / 100K), so its effect on the median
    // ns/call across OUTER rounds is negligible and conservative (slightly
    // inflates the measured cost, making the gate stricter, not looser).
    // The comment "each lookup pays the full radix walk" holds for >99.75%
    // of measured iterations; the first-256-per-round warm-up does not
    // compromise the gate intent.
    //
    // If kNumPaths is ever changed, keep it above ROUTE_CACHE_MAX_SIZE
    // (currently 256) so the LRU-eviction guarantee holds. If
    // ROUTE_CACHE_MAX_SIZE itself changes, this constant must be updated
    // manually -- see test-quality-reviewer-iter1-3.
    //
    // invalidate_route_cache() runs once per OUTER round (not per inner
    // iteration), so its cost (clearing a ≤256-entry map) is amortised
    // across INNER_RADIX (100K) lookups and does not taint the median.
    constexpr std::size_t kNumPaths = 320;  // > ROUTE_CACHE_MAX_SIZE (256)
    static const std::vector<std::string> kManyPaths =
        make_radix_paths(kNumPaths);

    std::printf("bench_route_lookup (b): radix_pure (cache cold, 8-segment "
                "parameterized path, %zu rotating paths)\n", kNumPaths);

    double median_radix_pure_ns = 0.0;
    {
        using clock = std::chrono::steady_clock;
        std::vector<double> samples_ns;
        samples_ns.reserve(OUTER);

        // Warmup: rotate through the path set once (cache live) to get the
        // trie and instruction cache hot before timing.
        std::size_t idx = 0;
        for (std::size_t i = 0; i < 10'000; ++i) {
            auto r = impl->lookup_v2(hs::http_method::get, kManyPaths[idx]);
            do_not_optimize(r);
            idx = (idx + 1) % kManyPaths.size();
        }

        for (std::size_t round = 0; round < OUTER; ++round) {
            impl->invalidate_route_cache();   // prelude: force cold cache
            const auto t0 = clock::now();
            for (std::size_t i = 0; i < INNER_RADIX; ++i) {
                auto r = impl->lookup_v2(hs::http_method::get, kManyPaths[idx]);
                do_not_optimize(r);
                idx = (idx + 1) % kManyPaths.size();
            }
            const auto t1 = clock::now();
            const double ns_per_call =
                std::chrono::duration<double, std::nano>(t1 - t0).count() /
                INNER_RADIX;
            samples_ns.push_back(ns_per_call);
        }
        const double min_ns =
            *std::min_element(samples_ns.begin(), samples_ns.end());
        const double max_ns =
            *std::max_element(samples_ns.begin(), samples_ns.end());
        median_radix_pure_ns = sort_and_median(samples_ns);
        const double p99 = p99_of_sorted(samples_ns);
        std::printf("  radix_pure: median=%.3fns  p99=%.3fns  "
                    "(min=%.3fns max=%.3fns)\n",
                    median_radix_pure_ns, p99, min_ns, max_ns);
    }

    // ----- Summary + gates -----
    std::printf("\nbench_route_lookup summary:\n");
    std::printf("  (a) cache_warm_ns median = %.3f ns/lookup  (ceiling %.1f ns)\n",
                median_cache_warm_ns, kCacheHitNsCeiling);
    std::printf("  (b) radix_pure_ns median = %.3f ns/lookup  (ceiling %.0f ns "
                "= %.1f us)\n",
                median_radix_pure_ns, kRadixNsCeiling, kRadixUsCeiling);

    int rc = 0;
    if (median_cache_warm_ns > kCacheHitNsCeiling) {
        std::printf("FAIL: (a) cache_warm_ns median %.3f ns exceeds ceiling "
                    "%.1f ns\n",
                    median_cache_warm_ns, kCacheHitNsCeiling);
        rc = 1;
    } else {
        std::printf("PASS: (a) cache_warm_ns within %.1f ns ceiling\n",
                    kCacheHitNsCeiling);
    }
    if (median_radix_pure_ns > kRadixNsCeiling) {
        std::printf("FAIL: (b) radix_pure_ns median %.3f ns exceeds ceiling "
                    "%.0f ns (%.1f us)\n",
                    median_radix_pure_ns, kRadixNsCeiling, kRadixUsCeiling);
        rc = 1;
    } else {
        std::printf("PASS: (b) radix_pure_ns within %.1f us ceiling\n",
                    kRadixUsCeiling);
    }
    return rc;
}
