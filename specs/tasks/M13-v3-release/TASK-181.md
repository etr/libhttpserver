### TASK-181: Expose bounded diagnostic callbacks and counters

**Milestone:** M13 - v3.0 release
**Component:** Observability
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide bounded diagnostic callbacks and counters for libhttpserver v3.0.

**Action Items:**
- [ ] Emit structured accept, stream, TLS, limit and drain diagnostics.
- [ ] Expose bounded counters without runtime logging dependency.
- [ ] Test redaction and callback dispatch outside I/O locks.

**Dependencies:**
- Blocked by: TASK-108, TASK-145, TASK-178
- Blocks: TASK-185

**Acceptance Criteria:**
- Connection, stream, TLS, limit and drain events are observable without logging secrets or blocking I/O locks.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-001

**Status:** Not Started
