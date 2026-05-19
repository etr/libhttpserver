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
// Algebra:
//   reduction := V1_HTTP_RESOURCE_SIZEOF - sizeof(http_resource)
// must satisfy
//   reduction + sizeof(method_set) * 2 >= V1_STD_MAP_STRING_BOOL_SIZEOF
//
// The `sizeof(method_set) * 2` term captures both the new member's
// own size and the worst-case alignment padding it forces (a 4-byte
// method_set in a struct with an 8-byte vptr is padded to the next
// pointer boundary, giving up to `sizeof(method_set)` of slack).
//
// Equivalently: `sizeof(http_resource) +
// V1_STD_MAP_STRING_BOOL_SIZEOF <= V1_HTTP_RESOURCE_SIZEOF +
// sizeof(method_set) * 2`.
//
// With macOS / libc++ baseline numbers (v1=32, map=24, v2=16,
// method_set=4): `16 + 24 = 40 <= 32 + 8 = 40` (tight, passes).
//
// With Linux / libstdc++ baseline numbers (v1=56, map=48, v2=16,
// method_set=4): `16 + 48 = 64 <= 56 + 8 = 64` (tight, passes).
//
// If a future refactor reintroduces a per-resource heap container or
// grows the bitmask storage, this assertion breaks at compile time.
static_assert(sizeof(http_resource) + V1_STD_MAP_STRING_BOOL_SIZEOF
              <= V1_HTTP_RESOURCE_SIZEOF + sizeof(method_set) * 2,
              "http_resource did not shrink by at least the empty-std::map "
              "footprint after the method_set refactor (PRD-REQ-REQ-003 / "
              "PRD §3.6)");

// Belt-and-suspenders: regardless of v1 anchoring, v2.0 must be
// strictly smaller than v1.
static_assert(sizeof(http_resource) < V1_HTTP_RESOURCE_SIZEOF,
              "http_resource did not shrink at all relative to v1");

int main() {
    return 0;
}
