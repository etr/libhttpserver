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

// (2) Conservative size upper bound: the config bag still lives on
//     webserver in TASK-014; only backend-coupled state moves into the
//     impl. The intent of this assertion is to fail loudly if a future
//     hand merges impl members back into webserver. TASK-019/020 will
//     tighten this to ~sizeof(void*) once the config bag also moves
//     into the impl. The pre-TASK-014 baseline was ~1600 bytes on
//     LP64 hosts; we bound the post-split layout at 144 pointers
//     (1152 bytes on LP64) so any regrowth is caught.
static_assert(sizeof(httpserver::webserver) <= 144 * sizeof(void*),
              "webserver size grew unexpectedly after PIMPL split");

int main() { return 0; }
