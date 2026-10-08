### TASK-154: Implement QUIC stream state and bounded reassembly

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC stream state and bounded reassembly for libhttpserver v3.0.

**Action Items:**
- [x] Implement QUIC stream IDs, state transitions and final-size rules.
- [x] Reassemble out-of-order STREAM ranges under gap/byte budgets.
- [x] Handle FIN, RESET_STREAM and STOP_SENDING once.

**Dependencies:**
- Blocked by: TASK-151
- Blocks: TASK-157

**Acceptance Criteria:**
- Reordered overlapping input yields ordered bytes; final-size and gap limits reject correctly.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-021
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress
