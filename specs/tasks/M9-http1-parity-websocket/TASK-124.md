### TASK-124: Freeze portable external-loop readiness contract

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Private operation I/O
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide portable external-loop readiness contract for libhttpserver v3.0.

**Action Items:**
- [ ] Define opaque socket keys, generations, interest snapshots and wake handle.
- [ ] Specify monotonic deadline and non-reentrant dispatch contract.
- [ ] Compile a platform-neutral consumer fixture.

**Dependencies:**
- Blocked by: TASK-099
- Blocks: TASK-125

**Acceptance Criteria:**
- Consumer fixture compiles on four OS families; generation, deadline, wake and non-reentrant dispatch rules are documented.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-015, PRD-V3N-REQ-016
**Related Decisions:** DR-V3-004

**Status:** Not Started
