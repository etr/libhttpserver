/*
     This file is part of libhttpserver
     Copyright (C) 2011-2026 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.
*/

// TASK-014: compile-time guarantees of the PIMPL split.
//
// We assert the structural invariants TASK-014 owns:
//   1. webserver is non-copyable and non-movable (PIMPL ABI lock-down).
//   2. sizeof(webserver) is bounded -- it should be the config bag plus
//      one impl_ pointer; everything backend-coupled lives behind that
//      pointer in detail/webserver_impl.hpp.
//
// Note: the literal "no <microhttpd.h>/<pthread.h> in <webserver.hpp>"
// grep is enforced by `make check-hygiene` on the staged install, and by
// `grep -E '#include\s+<microhttpd\.h>' src/httpserver/webserver.hpp` per
// the TASK-014 acceptance criteria. We do *not* repeat that as a runtime
// or preprocessor assertion here because <httpserver/http_utils.hpp> is
// still on the preprocessor side of the umbrella in TASK-014's scope --
// scrubbing that path is TASK-020's job (the existing XFAIL_TESTS gate).

// HTTPSERVER_COMPILATION is supplied by test/Makefile.am AM_CPPFLAGS.
#include "httpserver/webserver.hpp"

#include <type_traits>

// (1) PIMPL ABI lock-down: webserver owns a unique_ptr<webserver_impl>;
//     copying or moving would slice the impl. Make sure they're rejected.
static_assert(!std::is_copy_constructible_v<httpserver::webserver>,
              "webserver must not be copy-constructible");
static_assert(!std::is_copy_assignable_v<httpserver::webserver>,
              "webserver must not be copy-assignable");
static_assert(!std::is_move_constructible_v<httpserver::webserver>,
              "webserver must not be move-constructible");
static_assert(!std::is_move_assignable_v<httpserver::webserver>,
              "webserver must not be move-assignable");

// (2) Size bounds: webserver still owns the full config bag.
//     TASK-019 and TASK-020 shipped without moving config members into
//     the impl -- they were about backend-header hygiene, not member
//     migration -- so sizeof(webserver) is approximately:
//       N * sizeof(std::string) + 2 * sizeof(std::vector<std::string>)
//         + the scalar config fields + 1 * std::unique_ptr<webserver_impl>.
//     The std::string footprint is stdlib-dependent: libc++ is 24 bytes;
//     libstdc++ is 32 bytes.  That is why the libstdc++ lanes record a
//     larger observed size than the libc++ lane.
//
// Per-lane observed sizes (TASK-082; locked at max(observed) across
// every CI lane that compiles this TU; CI lanes are enumerated in
// .github/workflows/verify-build.yml; the ARM cross-compile lanes
// skip `make check` and therefore do NOT compile this gate):
//
//   | CI lane                                      | observed bytes |
//   |----------------------------------------------|----------------|
//   | macos-latest / Apple clang 21 / libc++       |            776 |  24-byte std::string SSO
//   | ubuntu-latest / gcc 11..14 / libstdc++       |           ~848 |  32-byte std::string SSO dominates (inferred from libstdc++ ABI; not directly measured on this lane)
//   | ubuntu-latest / clang 13..18 / libstdc++     |           ~848 |  same ABI as above (inferred, not directly measured on this lane)
//   | windows-latest / MINGW64 gcc / libstdc++     |           ~848 |  inferred from libstdc++ ABI (32-byte std::string SSO); not directly measured on this lane
//   | windows-latest / MSYS gcc / libstdc++        |           ~848 |  inferred from libstdc++ ABI (32-byte std::string SSO); not directly measured on this lane
//
// Slack: +16 bytes (one alignment-step worth of forgiveness for a
// padding shift across an ABI bump).  Tight enough that any new field
// on webserver trips this; loose enough to absorb one alignment-pad
// shift.  When this fires, the maintainer must:
//   1) re-measure on every lane that compiles this TU (use a probe
//      `static_assert(sizeof(...) == 0, ...)` line, push to CI, read
//      the numbers from each lane's compile log, then revert);
//   2) bump the threshold to max(observed) + 16;
//   3) update the table above.
//
// Threshold = max(observed) + 16 = 848 + 16 = 864.
static_assert(sizeof(httpserver::webserver) <= 864,
              "webserver size grew beyond the recorded per-lane "
              "max + 16-byte slack; see comment table above for the "
              "re-measurement procedure");

//     Lower bound (symmetrical): webserver must contain at least the
//     impl_ pointer itself. If someone accidentally links the backend
//     state back in without changing the public class, this catches
//     the case where the type somehow collapses to an empty shell.
static_assert(sizeof(httpserver::webserver) >= sizeof(void*),
              "webserver is suspiciously small: impl_ pointer may be missing");

int main() { return 0; }
