### TASK-153: Implement server CID admission, Retry and amplification limits

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide server CID admission, Retry and amplification limits for libhttpserver v3.0.

**Action Items:**
- [x] Demultiplex CIDs and bound pending Initial state.
- [x] Implement version negotiation, Retry and address token checks.
- [x] Track three-times amplification budget per unvalidated path.

**Dependencies:**
- Blocked by: TASK-149, TASK-151, TASK-152
- Blocks: TASK-155, TASK-164

**Acceptance Criteria:**
- Spoofed/truncated Initials cannot exceed state or three-times send limits; version negotiation works.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress
