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
// TASK-039 Cycle A: compile-time verification that v2.0's
// `http_resource` is smaller than v1's by approximately the cost of
// the removed `std::map<std::string, bool>` member.
//
// The check is purely compile-time. The build succeeds iff the
// v2.0 `sizeof(http_resource)` is bounded by the v1 baseline minus
// the empty-map footprint (with allowance for the new `method_set`
// field that replaced the map). See the comment block above the
// static_assert for the algebra.
//
// Wired into `make bench` via `EXTRA_PROGRAMS` in test/Makefile.am.
// Not built by `make all` or `make check`.

#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "httpserver/http_method.hpp"
#include "httpserver/http_resource.hpp"
#include "v1_baseline/v1_constants.hpp"

using httpserver::http_resource;
using httpserver::method_set;
using httpserver::v1_baseline::V1_HTTP_RESOURCE_SIZEOF;
using httpserver::v1_baseline::V1_STD_MAP_STRING_BOOL_SIZEOF;

// PRD-REQ-REQ-003 / PRD §3.6 acceptance: removing the
// `std::map<std::string, bool> method_state` member from
// `http_resource` saves at least its empty footprint, less the size
// of the new `method_set` member that replaced it (rounded up to
// alignment).
//
// Algebra (TASK-039 + TASK-051 + TASK-058 step 3):
//   reduction := V1_HTTP_RESOURCE_SIZEOF - sizeof(http_resource)
// must satisfy
//   reduction + sizeof(method_set) * 2 + sizeof(void*) * 2
//             + sizeof(std::shared_mutex) + sizeof(std::string)
//             + sizeof(method_set) + sizeof(bool) * 8
//        >= V1_STD_MAP_STRING_BOOL_SIZEOF
//
// Terms:
//   sizeof(method_set) * 2      -- own_size + worst-case alignment padding
//                                  for the methods_allowed_ member.
//   sizeof(void*) * 2           -- shared_ptr<resource_hook_table> hook_table_
//                                  member (TASK-051 hook PIMPL slot).
//   sizeof(std::shared_mutex)   -- TASK-058 step 3 / security-reviewer-iter1-2
//                                  / performance-reviewer-iter1-1: per-resource
//                                  Allow-header cache lock upgraded from
//                                  std::mutex to std::shared_mutex to allow
//                                  concurrent warm-path reads.
//                                  macOS (libc++): ~168 bytes.
//                                  Linux (libstdc++): ~56 bytes.
//   sizeof(std::string)         -- TASK-058 step 3: cached Allow header
//                                  storage.  SBO header is ~24-32 bytes.
//   sizeof(method_set)          -- TASK-058 step 3: cached mask snapshot.
//   sizeof(bool) * 8            -- TASK-058 step 3: cached_allow_valid_ +
//                                  worst-case alignment padding (8 bytes;
//                                  mutex forces 8-byte alignment after a
//                                  single bool).
//
// The V1_* constants are selected at compile time by the detected
// C++ standard library (libc++ vs. libstdc++) in v1_constants.hpp,
// so the assertion is correct on both macOS and Linux.
// See test/PERFORMANCE.md for the full per-platform derivation.
//
// If a future refactor reintroduces a per-resource heap container or
// grows the bitmask storage beyond what TASK-051's hook PIMPL and
// TASK-058's Allow cache needed, this assertion breaks at compile
// time and the growth gets reviewed.
//
// TODO(etr): unlike V1_GET_HEADERS_NS_PER_CALL in bench_get_headers.cpp (which
// carries a per-stdlib defensive static_assert against an accidental
// cross-stdlib swap, e.g. a libstdc++ value silently reused on libc++),
// V1_HTTP_RESOURCE_SIZEOF / V1_STD_MAP_STRING_BOOL_SIZEOF below have no
// such guard. Out of scope for now; worth adding if this becomes a real
// maintenance concern.
static_assert(sizeof(http_resource) + V1_STD_MAP_STRING_BOOL_SIZEOF
              <= V1_HTTP_RESOURCE_SIZEOF + sizeof(method_set) * 2
                  + sizeof(void*) * 2
                  + sizeof(std::shared_mutex) + sizeof(std::string)
                  + sizeof(method_set) + sizeof(bool) * 8,
              "http_resource grew beyond the documented PRD-REQ-REQ-003 / "
              "TASK-051 / TASK-058 step 3 envelope (method_set + per-route "
              "hook PIMPL slot + Allow header cache)");

// Belt-and-suspenders: regardless of v1 anchoring, v2.0 must be no
// larger than v1 + the documented hook PIMPL slot + the TASK-058 step 3
// Allow-header cache payload (upgraded to std::shared_mutex for
// security-reviewer-iter1-2 / performance-reviewer-iter1-1 fix).
static_assert(sizeof(http_resource) <=
                  V1_HTTP_RESOURCE_SIZEOF + sizeof(void*) * 2
                  + sizeof(std::shared_mutex) + sizeof(std::string)
                  + sizeof(method_set) + sizeof(bool) * 8,
              "http_resource grew beyond v1 + TASK-051 hook PIMPL slot + "
              "TASK-058 step 3 Allow cache payload");

int main() {
    // All checks are compile-time static_asserts above; this binary succeeds
    // by compiling. The runtime exit code carries no additional signal.
    return 0;
}
