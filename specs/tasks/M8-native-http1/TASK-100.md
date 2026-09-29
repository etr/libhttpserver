### TASK-100: Implement poll and WSAPoll socket backends

**Milestone:** M8 - Native HTTP/1
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide poll and WSAPoll socket backends for libhttpserver v3.0.

**Action Items:**
- [ ] Implement nonblocking poll and WSAPoll operation drivers.
- [ ] Add platform wake sources and monotonic deadline conversion.
- [ ] Run shared backend contract under slow-reader and hangup scenarios.

**Dependencies:**
- Blocked by: TASK-099
- Blocks: TASK-108, TASK-125, TASK-126, TASK-127, TASK-146, TASK-149

**Acceptance Criteria:**
- One HTTP/1 socket scenario passes the same backend contract on POSIX and Windows without busy looping.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-004, PRD-V3N-REQ-014
**Related Decisions:** DR-V3-004

**Status:** Not Started
