### TASK-167: Implement QPACK dynamic table and instruction codecs

**Milestone:** M12 - Full HTTP/3
**Component:** QPACK
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QPACK dynamic table and instruction codecs for libhttpserver v3.0.

**Action Items:**
- [ ] Implement QPACK insert, duplicate, capacity and eviction instructions.
- [ ] Track absolute/relative/post-base references.
- [ ] Replay RFC dynamic table vectors and malformed-index errors.

**Dependencies:**
- Blocked by: TASK-159
- Blocks: TASK-168

**Acceptance Criteria:**
- RFC dynamic vectors pass and referenced entries cannot be evicted prematurely.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-017
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
