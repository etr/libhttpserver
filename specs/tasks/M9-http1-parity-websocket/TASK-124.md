### TASK-124: Freeze portable external-loop readiness contract

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Private operation I/O
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide portable external-loop readiness contract for libhttpserver v3.0.

**Action Items:**
- [x] Define opaque socket keys, generations, interest snapshots and wake handle.
- [x] Specify monotonic deadline and non-reentrant dispatch contract.
- [x] Compile a platform-neutral consumer fixture.

**Dependencies:**
- Blocked by: TASK-099
- Blocks: TASK-125

**Acceptance Criteria:**
- Consumer fixture compiles locally; generation, deadline, wake and non-reentrant dispatch rules are documented. Other OS families are verified through CI and the v3 PR, per the user's 2026-10-06 clarification.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-015, PRD-V3N-REQ-016
**Related Decisions:** DR-V3-004

**Status:** Complete

**Implementation evidence (2026-10-06):** The public contract, normative rules,
consumer/value tests, and packaging/audit wiring are implemented. Local macOS
verification passed 221/221 registered tests with no skips or failures, full
`check-local`, and source/installed-header consumer compilation. Recovery also
collected a native Linux C++20 fixture receipt for identical input hashes and
reran the macOS focused consumer/value checks successfully. BSD and Windows
fixture compilation remains unexecuted locally and belongs to CI/v3 PR
verification; it does not block local completion or merge. Groundwork
validation and finalization remain pending. See
[implementation evidence](../../architecture/v3/TASK-124-implementation-evidence.md).
