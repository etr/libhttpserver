### TASK-158: Implement QUIC congestion control, pacing and fair send scheduling

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC congestion control, pacing and fair send scheduling for libhttpserver v3.0.

**Action Items:**
- [x] Implement NewReno congestion window, recovery and pacing.
- [x] Schedule streams fairly with control-plane reserve.
- [x] Test loss, persistent congestion and probe allowance on fake clock.

**Dependencies:**
- Blocked by: TASK-156, TASK-157
- Blocks: TASK-161, TASK-164, TASK-165

**Acceptance Criteria:**
- NewReno baseline reacts to loss correctly and reserves capacity for handshake/control traffic.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-008, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete

**Implementation evidence:** [QUIC scheduling contract and local checks](../../../docs/task-158-quic-scheduling.md). Caller-owned validation and finalization remain pending.
