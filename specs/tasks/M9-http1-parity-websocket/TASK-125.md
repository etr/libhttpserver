### TASK-125: Implement external-loop readiness adapter

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide external-loop readiness adapter for libhttpserver v3.0.

**Action Items:**
- [ ] Translate host readiness and timer events into private operations.
- [ ] Ignore stale generations and handle close/reopen reuse.
- [ ] Run an external-loop HTTP/1 host example.

**Dependencies:**
- Blocked by: TASK-100, TASK-124
- Blocks: TASK-182

**Acceptance Criteria:**
- Host-driven HTTP/1 works with stale events, descriptor reuse, timer expiry and wake races.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-015
**Related Decisions:** DR-V3-004

**Status:** Not Started
