### TASK-151: Implement strict QUIC v1 packet, frame and parameter codecs

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide strict QUIC v1 packet, frame and parameter codecs for libhttpserver v3.0.

**Action Items:**
- [x] Implement varints, packet numbers, long/short headers and frames.
- [x] Parse transport parameters with strict duplicate and length checks.
- [x] Replay vectors and malformed-corpus inputs without proportional allocation.

**Dependencies:**
- Blocked by: TASK-150
- Blocks: TASK-152, TASK-153, TASK-154, TASK-156

**Acceptance Criteria:**
- Varint, packet/header and transport-parameter vectors pass; malformed lengths and overflow fail before allocation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress
