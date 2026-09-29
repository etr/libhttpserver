### TASK-104: Implement response writer, body source and backpressure

**Milestone:** M8 - Native HTTP/1
**Component:** Exchange and body streaming
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide response writer, body source and backpressure for libhttpserver v3.0.

**Action Items:**
- [ ] Define typed body-source data/end/failure and one outstanding writer operation.
- [ ] Bound output queues and resume writes on capacity.
- [ ] Cancel pending writers and close sources exactly once.

**Dependencies:**
- Blocked by: TASK-102
- Blocks: TASK-107, TASK-111, TASK-112, TASK-113, TASK-121, TASK-141, TASK-161

**Acceptance Criteria:**
- A blocked sink bounds queued bytes and wakes one outstanding write; source reports data/end/error through typed results.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-026, PRD-V3N-REQ-027, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-003

**Status:** Not Started
