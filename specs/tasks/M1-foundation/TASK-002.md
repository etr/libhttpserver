### TASK-002: Public/private header layout and inclusion guards

**Milestone:** M1 - Foundation
**Component:** Header layout
**Estimate:** M

**Goal:**
Lock the public/private header split so PIMPL impls and detail headers can never escape the installed surface, and so consumers must come in through `<httpserver.hpp>`.

**Action Items:**
- [x] Add `#ifndef _HTTPSERVER_HPP_INSIDE_ \n#error "Include httpserver.hpp" \n#endif` (or `HTTPSERVER_COMPILATION` for first-party TUs) to every public header in `src/httpserver/*.hpp`. (pre-existing dual-mode gate retained per plan Phase 3a-i; TASK-014 to tighten to HTTPSERVER_COMPILATION-only)
- [x] Add `#ifndef HTTPSERVER_COMPILATION \n#error "internal header" \n#endif` to every header in `src/httpserver/details/`. (dual-mode gate kept: `!defined(_HTTPSERVER_HPP_INSIDE_) && !defined(HTTPSERVER_COMPILATION)`; TASK-014 to tighten once transitive include from webserver.hpp is removed)
- [x] Confirm `Makefile.am` installs `httpserver/*.hpp` and `excludes httpserver/details/*.hpp` from `make install`. (noinst_HEADERS in src/Makefile.am; detail/ excluded from install)
- [x] Define `_HTTPSERVER_HPP_INSIDE_` (and `#undef` it at end) inside `src/httpserver.hpp`.
- [x] Define `HTTPSERVER_COMPILATION` in `Makefile.am`'s build flags (only for the library's own TUs and tests). (set via per-directory AM_CPPFLAGS in src/Makefile.am and test/Makefile.am)

**Dependencies:**
- Blocked by: TASK-001
- Blocks: TASK-003, TASK-004, TASK-005, TASK-006, TASK-007, TASK-008, TASK-014, TASK-015

**Acceptance Criteria:**
- A consumer TU containing only `#include <httpserver/webserver.hpp>` (without the umbrella header) fails to compile with the gate error.
- `make install` followed by `find $prefix/include -name '*_impl.hpp' -o -name 'details'` returns nothing.
- All v1 tests still build (they go through `<httpserver.hpp>` already).
- Typecheck passes.

**Related Requirements:** PRD-HDR-REQ-001..003
**Related Decisions:** DR-002

**Status:** Done
