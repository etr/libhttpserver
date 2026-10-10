### TASK-192: Implement owned v3 cookie helpers and parity witnesses

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Cookie semantics and behavior parity inventory
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Supply the missing bounded, owned public v3 cookie value/parser/renderer and genuine per-case parity evidence, preserving documented cookie semantics through typed outcomes and ordered fields.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-192, require `base_branch=v3`, RED/GREEN for parity behavior, then Groundwork validation/finalization.

**Action Items:**
- [x] Implement an owned `httpserver::http` cookie value with explicit parse/render operations, typed atomic render failure and installation coverage; compare interfaces and clear public API doubts before commit. No cookie jar, v2 shims or new request/response hierarchy.
- [x] Reproduce missing-target failure from `task179-capabilities.json` capability-054. Inspect legacy `cookie_render_test.cpp` dependencies versus current installed `http/cookie*` and existing native cookie suites; do not blindly re-enable obsolete v2 tests/API.
- [x] Add or register a native target that verifies the inventory's three named behaviors: multi-cookie parsing, full canonical Set-Cookie attribute ordering, and rejection of CR in parsed/rendered values. Port assertions to current public v3 types or map them to already equivalent named native tests with an explicit inventory correction.
- [x] Add a failing parity runner contract for an absent/mismatched suite/case and positive matching executed evidence. Retain per-case identity/counts; no inventory-only migration note may substitute for current implementation evidence.
- [x] Verify cookie wire parse/response round trips and run the complete TASK-179 parity inventory, resolving only directly caused mapping failures. Package new targets/data and document any actual behavior changes in migration notes.

**Implementation evidence:** [TASK-192 evidence](../../architecture/v3/TASK-192-implementation-evidence.md). All local implementation gates pass; caller validation/finalization/merge/cleanup remain pending.

**Acceptance Criteria:**
- Required cookie behaviors have executable current-v3 witnesses and matching per-case receipts in the full parity run; no missing `cookie_render` target or hidden v2 link dependency.
- Normal/ASan C++20 TLS-off/on native cookie tests and parity runner contracts pass; relevant installed cookie consumer compiles/runs.
- Groundwork validation, evidence/tracking, merge and cleanup complete.

**Dependencies:**
- Blocked by: TASK-191
- Blocks: TASK-193, TASK-185
**Related Requirements:** PRD-V3N-REQ-017, PRD-V3N-REQ-018, PRD-V3N-REQ-037, PRD-V3N-REQ-038.
**Related Decisions:** DR-V3-001; architecture §4 owned compatibility helpers.
**Status:** In Progress
