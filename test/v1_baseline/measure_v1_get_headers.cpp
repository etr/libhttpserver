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
// TASK-039 -- one-off measurement TU.
//
// Capture v1's `get_headers()` median ns/call against a 16-header
// request, for the V1_GET_HEADERS_NS_PER_CALL constant consumed by
// test/bench_get_headers.cpp via test/v1_baseline/v1_constants.hpp.
//
// This TU is NOT built by `make bench` or `make check`. It ships in
// EXTRA_DIST as documentation. See test/v1_baseline/README.md.
//
// Strategy: v1's get_headers() goes through MHD_get_connection_values
// which invokes a callback once per header. We override that single
// MHD entrypoint with a stub that synthesises 16 (key, value) pairs,
// then time the v1 function body in a tight loop. This isolates the
// std::map construction / insertion / destruction cost (the dominant
// term in v1's per-call signature) without requiring a live MHD
// daemon.
//
// The function body we time is a structural transcription of
// master:src/http_request.cpp:177 (`get_headerlike_values`); the
// transcription is necessary because the v1 method is non-static and
// the v1 library is not linked here.
//
// Build (run from feature/v2.0 .worktrees/TASK-039 against the v1
// header tree):
//   c++ -std=c++20 -O3 \
//       -I/opt/homebrew/include \
//       test/v1_baseline/measure_v1_get_headers.cpp \
//       -o /tmp/measure_v1_get_headers
//   /tmp/measure_v1_get_headers
//
// Expected output (libc++ on Darwin/arm64, master @ d8b055e):
//   v1_get_headers_ns_per_call=~770
//
// Run on a quiet release-mode host: no concurrent load, no power
// throttling. Take the median of 10 outer reps.

#include <microhttpd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Storage for 16 synthetic (key, value) pairs that outlive the timed
// loop. The arrays of pointers are what the MHD callback hands back
// to the v1 build_request_header (it takes `const char*` from MHD).
const char* g_keys[16];
const char* g_vals[16];

struct StaticHeaders {
    std::vector<std::string> keys;
    std::vector<std::string> vals;
    StaticHeaders() {
        keys.reserve(16);
        vals.reserve(16);
        for (int i = 0; i < 16; ++i) {
            char k[32];
            char v[32];
            std::snprintf(k, sizeof(k), "X-Bench-%02d", i);
            std::snprintf(v, sizeof(v), "v%02d", i);
            keys.emplace_back(k);
            vals.emplace_back(v);
        }
        for (int i = 0; i < 16; ++i) {
            g_keys[i] = keys[i].c_str();
            g_vals[i] = vals[i].c_str();
        }
    }
} g_static_headers;

}  // namespace

// Stub for MHD_get_connection_values: invoke the v1 callback 16
// times with our synthetic headers. We never link real MHD into this
// TU, so the linker picks ours.
extern "C" int MHD_get_connection_values(struct MHD_Connection* connection,
                                         enum MHD_ValueKind kind,
                                         MHD_KeyValueIterator iter,
                                         void* cls) {
    (void)connection;
    (void)kind;
    if (iter == nullptr) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < 16; ++i) {
        if (iter(cls, MHD_HEADER_KIND, g_keys[i], g_vals[i]) == MHD_NO) {
            break;
        }
        ++count;
    }
    return count;
}

namespace v1 {

// Transcription of v1's http::header_view_map (an ordered map from
// std::string to std::string). v1's actual definition uses a
// case-insensitive comparator; for the bench we only need the same
// node footprint and insertion cost, so a default `std::less` is
// fine -- the std::map node layout is independent of comparator.
using header_view_map = std::map<std::string, std::string>;

// Transcription of v1's free-standing build_request_header (the
// callback passed to MHD_get_connection_values).
MHD_Result build_request_header(void* cls, enum MHD_ValueKind kind,
                                const char* key, const char* value) {
    (void)kind;
    auto* dhr = static_cast<header_view_map*>(cls);
    (*dhr)[key] = value;
    return MHD_YES;
}

// Transcription of v1's http_request::get_headerlike_values body.
header_view_map get_headerlike_values(struct MHD_Connection* conn,
                                      enum MHD_ValueKind kind) {
    header_view_map headers;
    MHD_get_connection_values(conn, kind, &build_request_header,
                              reinterpret_cast<void*>(&headers));
    return headers;
}

}  // namespace v1

template <typename T>
[[gnu::always_inline]] inline void do_not_optimize(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

int main() {
    using clock = std::chrono::steady_clock;

    for (int i = 0; i < 10000; ++i) {
        auto m = v1::get_headerlike_values(nullptr, MHD_HEADER_KIND);
        do_not_optimize(m);
    }

    constexpr int OUTER = 10;
    constexpr int INNER = 1'000'000;
    std::vector<double> samples_ns;
    samples_ns.reserve(OUTER);
    for (int r = 0; r < OUTER; ++r) {
        auto t0 = clock::now();
        for (int i = 0; i < INNER; ++i) {
            auto m = v1::get_headerlike_values(nullptr, MHD_HEADER_KIND);
            do_not_optimize(m);
        }
        auto t1 = clock::now();
        double ns_per_call =
            std::chrono::duration<double, std::nano>(t1 - t0).count() / INNER;
        samples_ns.push_back(ns_per_call);
    }
    std::sort(samples_ns.begin(), samples_ns.end());
    double median = samples_ns[OUTER / 2];

    std::printf("v1_get_headers_ns_per_call=%.3f\n", median);
    std::printf("  (min=%.3f max=%.3f)\n",
                samples_ns.front(), samples_ns.back());
    return 0;
}
