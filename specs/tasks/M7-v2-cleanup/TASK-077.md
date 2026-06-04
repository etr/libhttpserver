### TASK-077: Restore Windows / Darwin coverage in skipped test suites

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/threaded.cpp`, `test/integ/ws_start_stop.cpp`, `test/integ/authentication.cpp`
**Estimate:** L

**Goal:**
Three whole-suite or near-whole-suite skips remove the library's portability claim from CI:
- `test/integ/threaded.cpp:61-95` — entire suite body `#ifndef _WINDOWS`; Windows shows zero coverage.
- `test/integ/ws_start_stop.cpp:113-1389` — wide `#ifndef _WINDOWS` over most of the file, with no compensating Win-only suite.
- `test/integ/ws_start_stop.cpp:176-180` + `authentication.cpp:176-180` — Windows digest-auth tests explicitly postponed.
- `test/integ/ws_start_stop.cpp:337` — `#ifndef DARWIN` over `custom_socket` with no comment.

Either land Windows/Darwin variants or document and gate the exclusion behind a CI flag that asserts intent rather than letting silent skips drift.

**Action Items:**
- [ ] For `threaded.cpp`: write a Windows-shaped variant of the suite (matching the I/O completion port semantics MHD uses on Windows) under `#ifdef _WINDOWS`, or document why no Windows variant is feasible and capture the gap in `test/PORTABILITY.md`.
- [ ] For `ws_start_stop.cpp` Windows skip: same — variant suite under `#ifdef _WINDOWS`, or PORTABILITY.md note. Comment must explain *why* MHD's Windows path can't drive this scenario.
- [ ] For the postponed digest-auth Windows tests in `ws_start_stop.cpp:176-180` and `authentication.cpp:176-180`: either port them or document the upstream MHD-on-Windows limitation. If coordinated with TASK-062 (Digest RFC-7616), bring up there.
- [ ] For `ws_start_stop.cpp:337` `custom_socket` Darwin skip: add a comment naming the macOS-specific limitation, or port. Comment must reference the specific Darwin syscall behaviour that fails.
- [ ] Add a `scripts/check-skip-rationales.sh` lint that fails CI if a `#ifndef _WINDOWS` or `#ifndef DARWIN` block in `test/integ/` lacks a `// reason:` comment.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- Every `#ifndef _WINDOWS` / `#ifndef DARWIN` block in `test/integ/` carries a `// reason:` comment that the lint script verifies.
- At least one Windows-shaped variant exists for the threaded and ws_start_stop suites (or PORTABILITY.md records the documented gap).
- Windows lane in `verify-build.yml` exercises the new variants where added.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 portability NFR
**Related Decisions:** None new

**Status:** Backlog
