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
// Algebra (TASK-039 + TASK-051):
//   reduction := V1_HTTP_RESOURCE_SIZEOF - sizeof(http_resource)
// must satisfy
//   reduction + sizeof(method_set) * 2 + sizeof(void*) * 2
//        >= V1_STD_MAP_STRING_BOOL_SIZEOF
//
// The `sizeof(method_set) * 2` term is a conservative upper bound:
// own_size + max_padding <= 2 * own_size (when alignment <= sizeof(member)).
// It is not an exact count but a worst-case allowance for the new field's
// own size plus the largest possible alignment padding it could force.
// `sizeof(void*) * 2` captures the
// shared_ptr<detail::resource_hook_table> hook_table_ member added in
// TASK-051 (PIMPL slot for per-route lifecycle hooks).
//
// Equivalently: `sizeof(http_resource) +
// V1_STD_MAP_STRING_BOOL_SIZEOF <= V1_HTTP_RESOURCE_SIZEOF +
// sizeof(method_set) * 2 + sizeof(void*) * 2`.
//
// The V1_* constants are selected at compile time by the detected
// C++ standard library (libc++ vs. libstdc++) in v1_constants.hpp,
// so the assertion is correct on both macOS and Linux.
// See test/PERFORMANCE.md for the full per-platform derivation.
//
// If a future refactor reintroduces a per-resource heap container or
// grows the bitmask storage beyond what TASK-051's hook PIMPL needed,
// this assertion breaks at compile time and the growth gets reviewed.
static_assert(sizeof(http_resource) + V1_STD_MAP_STRING_BOOL_SIZEOF
              <= V1_HTTP_RESOURCE_SIZEOF + sizeof(method_set) * 2
                  + sizeof(void*) * 2,
              "http_resource grew beyond the documented PRD-REQ-REQ-003 / "
              "TASK-051 envelope (method_set + per-route hook PIMPL slot)");

// Belt-and-suspenders: regardless of v1 anchoring, v2.0 must be no
// larger than v1 + the documented hook PIMPL slot (one shared_ptr).
static_assert(sizeof(http_resource) <=
                  V1_HTTP_RESOURCE_SIZEOF + sizeof(void*) * 2,
              "http_resource grew beyond v1 + TASK-051 hook PIMPL slot");

int main() {
    // All checks are compile-time static_asserts above; this binary succeeds
    // by compiling. The runtime exit code carries no additional signal.
    return 0;
}
