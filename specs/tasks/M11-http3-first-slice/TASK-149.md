### TASK-149: Add owned UDP send/receive operations and CID dispatch seam

**Milestone:** M11 - First HTTP/3 slice
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide owned UDP send/receive operations and CID dispatch seam for libhttpserver v3.0.

**Action Items:**
- [ ] Add bounded UDP receive/send operations on supported backends.
- [ ] Extract invariant header and route by destination CID.
- [ ] Preserve packet and address metadata through connection-owner dispatch.

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-150, TASK-153

**Acceptance Criteria:**
- Datagrams route by destination connection ID through bounded per-connection queues on supported I/O backends.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-004

**Status:** Not Started
