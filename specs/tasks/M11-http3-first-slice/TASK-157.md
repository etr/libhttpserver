### TASK-157: Implement QUIC stream/connection flow control

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC stream/connection flow control for libhttpserver v3.0.

**Action Items:**
- [x] Track MAX_DATA, MAX_STREAM_DATA and stream-count limits.
- [x] Release credit only when semantic body bytes are consumed.
- [x] Reserve control and CRYPTO capacity during data stalls.

**Dependencies:**
- Blocked by: TASK-154, TASK-156
- Blocks: TASK-158, TASK-160, TASK-163, TASK-166

**Acceptance Criteria:**
- Application consumption releases credit; stalled streams bound memory without blocking control work.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-008, PRD-V3N-REQ-021, PRD-V3N-REQ-025, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress

**Implementation evidence:** [QUIC flow-control ownership, bounds and local checks](../../../docs/task-157-quic-flow-control.md)
