### TASK-168: Implement QPACK blocked-section and critical-stream accounting

**Milestone:** M12 - Full HTTP/3
**Component:** QPACK
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QPACK blocked-section and critical-stream accounting for libhttpserver v3.0.

**Action Items:**
- [ ] Track QPACK blocked count and retained encoded bytes independently.
- [ ] Process section acknowledgments and cancellation.
- [ ] Reserve encoder/decoder stream credit and unblock reordered headers.

**Dependencies:**
- Blocked by: TASK-160, TASK-167
- Blocks: TASK-169

**Acceptance Criteria:**
- Out-of-order inserts unblock requests within independent blocked-count and retained-byte budgets.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
