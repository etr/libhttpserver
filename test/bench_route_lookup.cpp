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
//   (a) cache-hit ceiling      -- 200 ns / lookup (median)
//   (b) radix tier ceiling     -- 5 us / lookup for 8-segment paths
//                                 (median, cold-cache)
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

namespace hs = httpserver;

// Defeat dead-store elimination on the lookup_result.
template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(&value) : "memory");
#else
    volatile const void* sink = static_cast<const void*>(&value);
    (void)sink;
#endif
}

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

    std::printf("bench_route_lookup (a): cache-hit (/api/v1/users/me)\n");
    const double median_cache_ns = measure_median_ns(
        "cache-hit",
        [&]() {
            auto r = impl->lookup_v2(hs::http_method::get, kCacheHitPath);
            do_not_optimize(r);
        },
        OUTER, INNER_CACHE);

    // ----- (b) radix tier, 8 segments -----
    // Vary the captured-param values across iterations so we don't
    // measure pure cache-hits. The PATHS table holds 16 distinct
    // 8-segment URLs; the modulo index rotates through them.
    // NOTE: route_lru_cache holds 256 entries by default, so all 16
    // paths fit warm after the first rotation — the bench therefore
    // measures a mix of cache-warm latency (most iterations) and one
    // radix walk per fresh URL per round. The 5 µs ceiling is
    // conservative enough to absorb the mix. If detail::route_cache's
    // default capacity is ever shrunk below 16, this bench would
    // shift to measuring pure radix-tier latency.
    static const std::vector<std::string> kPaths = {
        "/a/u01/b/v01/c/w01/d/x01", "/a/u02/b/v02/c/w02/d/x02",
        "/a/u03/b/v03/c/w03/d/x03", "/a/u04/b/v04/c/w04/d/x04",
        "/a/u05/b/v05/c/w05/d/x05", "/a/u06/b/v06/c/w06/d/x06",
        "/a/u07/b/v07/c/w07/d/x07", "/a/u08/b/v08/c/w08/d/x08",
        "/a/u09/b/v09/c/w09/d/x09", "/a/u10/b/v10/c/w10/d/x10",
        "/a/u11/b/v11/c/w11/d/x11", "/a/u12/b/v12/c/w12/d/x12",
        "/a/u13/b/v13/c/w13/d/x13", "/a/u14/b/v14/c/w14/d/x14",
        "/a/u15/b/v15/c/w15/d/x15", "/a/u16/b/v16/c/w16/d/x16",
    };
    std::size_t idx = 0;
    std::printf("bench_route_lookup (b): radix tier, 8-segment "
                "parameterized path\n");
    const double median_radix_ns = measure_median_ns(
        "radix-8seg",
        [&]() {
            auto r = impl->lookup_v2(
                hs::http_method::get, kPaths[idx]);
            do_not_optimize(r);
            idx = (idx + 1) % kPaths.size();
        },
        OUTER, INNER_RADIX);

    // ----- Summary + gates -----
    std::printf("\nbench_route_lookup summary:\n");
    std::printf("  (a) cache-hit  median = %.3f ns/lookup  (ceiling %.1f ns)\n",
                median_cache_ns, kCacheHitNsCeiling);
    std::printf("  (b) radix-8seg median = %.3f ns/lookup  (ceiling %.0f ns "
                "= %.1f us)\n",
                median_radix_ns, kRadixNsCeiling, kRadixUsCeiling);

    int rc = 0;
    if (median_cache_ns > kCacheHitNsCeiling) {
        std::printf("FAIL: (a) cache-hit median %.3f ns exceeds ceiling "
                    "%.1f ns\n",
                    median_cache_ns, kCacheHitNsCeiling);
        rc = 1;
    } else {
        std::printf("PASS: (a) cache-hit within %.1f ns ceiling\n",
                    kCacheHitNsCeiling);
    }
    if (median_radix_ns > kRadixNsCeiling) {
        std::printf("FAIL: (b) radix-8seg median %.3f ns exceeds ceiling "
                    "%.0f ns (%.1f us)\n",
                    median_radix_ns, kRadixNsCeiling, kRadixUsCeiling);
        rc = 1;
    } else {
        std::printf("PASS: (b) radix-8seg within %.1f us ceiling\n",
                    kRadixUsCeiling);
    }
    return rc;
}
