### TASK-007: CI test for public-header hygiene

**Milestone:** M1 - Foundation
**Component:** CI / Test infrastructure
**Estimate:** S

**Goal:**
Lock in the "no backend headers leak through `<httpserver.hpp>`" invariant with a CI gate so a future commit can't silently regress it.

**Action Items:**
- [x] Add a test program `test/header_hygiene.cpp` containing only `#include <httpserver.hpp>` and `int main(){}`. *(Implemented as `test/unit/header_hygiene_test.cpp` for test-tree symmetry; `test/headers/consumer_umbrella_no_backend.cpp` is the parallel source consumed by the preprocessor-grep target.)*
- [x] In `Makefile.am`, build it without `-I` flags pointing at libmicrohttpd / pthread / gnutls headers (use only the installed-header path). *(Per-target `header_hygiene_CPPFLAGS = -I$(top_srcdir)/src $(CPPFLAGS)` overrides `AM_CPPFLAGS`, dropping `-DHTTPSERVER_COMPILATION` and `-I$(top_srcdir)/src/httpserver/`. The preprocessor-grep target uses ONLY the staged `DESTDIR` install include path.)*
- [x] Run `g++ -E test/header_hygiene.cpp -I<install-prefix>/include` and `grep -E 'microhttpd\.h|pthread\.h|gnutls/gnutls\.h|sys/socket\.h|sys/uio\.h'` — expect zero matches. *(See `check-hygiene` in top-level `Makefile.am`. Today the grep finds matches; that's the EXPECTED-FAIL state until M5.)*
- [x] Wire the check into `make check` (or a dedicated `make hygiene` target invoked by CI). *(Both: the runtime sentinel `header_hygiene` runs as part of `make check` (XFAIL until M5); the preprocessor-grep `check-hygiene` runs via `check-local` and also stands alone as a target for CI.)*
- [x] Add a CI job that fails if any of the forbidden headers appear in the preprocessed output. *(Added `header-hygiene` matrix entry in `.github/workflows/verify-build.yml` running `make check-hygiene`. Currently informational; flips to fatal at TASK-020 by setting `HEADER_HYGIENE_STRICT=yes`.)*

**Dependencies:**
- Blocked by: TASK-002
- Blocks: None (informational gate; will fail until M2-M5 land, that's expected and intended)

**Acceptance Criteria:**
- `grep -lE 'microhttpd\.h|pthread\.h|gnutls\.h|sys/socket\.h' src/httpserver/*.hpp` returns no results once M2-M5 land (PRD §3.1 acceptance).
- The hygiene test is invoked by `make check` and fails loudly when violated.
- Typecheck passes.

**Related Requirements:** PRD-HDR-REQ-001..003
**Related Decisions:** §9 testing item 1

**Status:** Done (informational gate landed; full enforcement at TASK-020)

---

**Implementation Notes (TASK-007 close-out):**

- **Strategy:** Option (c) from the plan -- "implement the test machinery now, mark it XFAIL until M5." Rejected (a) "leave `make check` red" (would block every PR for weeks); rejected (b) "narrow the grep to today's leaks" (encodes a binary invariant as a moving target, four chances to forget).
- **Two layers of enforcement, both wired into `make check`:**
  - *Layer 1 (compile-time sentinel):* `test/unit/header_hygiene_test.cpp` includes `<httpserver.hpp>` then checks well-known include-guard macros (`MHD_VERSION`, `_PTHREAD_H{,_}`, `GNUTLS_GNUTLS_H`, `_SYS_SOCKET_H{,_}`, `_SYS_UIO_H{,_}`). At runtime it prints the leaked headers and exits 1. Marked `XFAIL_TESTS` in `test/Makefile.am` so `make check` stays green.
  - *Layer 2 (preprocessor grep):* `make check-hygiene` in the top-level `Makefile.am` stages `make install DESTDIR=$(CHECK_HYGIENE_STAGE)` and preprocesses `test/headers/consumer_umbrella_no_backend.cpp` against ONLY the staged include path, then greps cpp line markers for forbidden headers. Default `HEADER_HYGIENE_STRICT=no` makes it informational; flipping to `yes` makes it fatal.
- **CI:** dedicated `header-hygiene` matrix entry in `.github/workflows/verify-build.yml` invokes `make check-hygiene` so the gate surfaces as its own GitHub Actions check.
- **`<sys/uio.h>` rationale:** PRD-HDR-REQ-001..003 don't name `<sys/uio.h>` directly, but TASK-004 introduced `iovec_entry` specifically to avoid exposing it. Listing it here is a hardening assertion that TASK-004's intent isn't regressed.
- **Why preprocessor-grep currently fails ahead of leak detection:** the staged install does not ship `details/` headers (per TASK-002); `webserver.hpp` still references `httpserver/details/http_endpoint.hpp` until TASK-014's PIMPL split. The `check-hygiene` recipe treats this preprocessor failure as EXPECTED-FAIL in informational mode, with diagnostics so M2-M5 progress remains visible.

**M5 close-out (TASK-020 owner: zero ambiguity):**

When TASK-020 makes `<httpserver.hpp>` clean of backend headers:

1. Run `make check-hygiene HEADER_HYGIENE_STRICT=yes` from the build dir -- confirm exit 0 and `PASS: no forbidden headers reached the consumer TU`.
2. Run `make check` -- expect Automake to report `XPASS: header_hygiene` (treated as a hard error by default), confirming the sentinel now passes.
3. In `test/Makefile.am`, delete the line `XFAIL_TESTS = header_hygiene` and the comment block above it. Re-run `make check` -- expect `PASS: header_hygiene` and overall green.
4. In `Makefile.am`, change `HEADER_HYGIENE_STRICT ?= no` to `HEADER_HYGIENE_STRICT ?= yes` (or remove the conditional and inline the strict path). Re-run `make check` to confirm `check-hygiene` is green.
5. Mark this task `Status: Done (full enforcement)` and tick the M5 acceptance criterion (`grep -lE '...' src/httpserver/*.hpp` returns no results).
