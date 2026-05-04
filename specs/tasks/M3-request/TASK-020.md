### TASK-020: Final public-header backend-include sweep

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** Public headers (sweep)
**Estimate:** S

**Goal:**
Verify and lock the "no backend headers in public surface" invariant after PIMPL splits and accessor refactors land, removing any straggler includes that survived earlier tasks.

**Action Items:**
- [ ] `grep -lE 'microhttpd\.h|pthread\.h|gnutls/gnutls\.h|sys/socket\.h|sys/uio\.h' src/httpserver/*.hpp`. Each file that turns up: route the include into the corresponding `detail/*_impl.hpp` or `.cpp` file.
- [ ] Verify after the sweep that the grep returns zero results.
- [ ] Ensure the hygiene CI test from TASK-007 now passes. **Specifically:**
  - [ ] In `test/Makefile.am`, delete the line `XFAIL_TESTS = header_hygiene` (and the explanatory comment block above it). After this edit, `make check` should report `PASS: header_hygiene` -- not `XFAIL` and not `XPASS`.
  - [ ] In `Makefile.am`, change `HEADER_HYGIENE_STRICT ?= no` to `HEADER_HYGIENE_STRICT ?= yes` (or remove the conditional and inline the strict-mode path). Verify `make check-hygiene` exits 0 with `PASS: no forbidden headers reached the consumer TU`.
  - [ ] Run `make check-hygiene HEADER_HYGIENE_STRICT=yes` from the build dir as a final smoke check.

**Dependencies:**
- Blocked by: TASK-014, TASK-015, TASK-019
- Blocks: None (gating outcome that the rest of the project relies on)

**Acceptance Criteria:**
- `grep -lE 'microhttpd\.h|pthread\.h|gnutls\.h|sys/socket\.h' src/httpserver/*.hpp` returns no results (PRD §3.1 acceptance).
- A test program containing only `#include <httpserver.hpp>` and `int main(){}` compiles without `-I` to libmicrohttpd / pthread / gnutls (PRD §3.1 acceptance).
- TASK-007's hygiene test (red until now) goes green.
- Typecheck passes.

**Related Requirements:** PRD-HDR-REQ-001, PRD-HDR-REQ-002, PRD-HDR-REQ-003
**Related Decisions:** §2.2, §5.5

**Status:** Not Started
