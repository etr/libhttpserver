### TASK-099: Implement private operation I/O contract and fake backend

**Milestone:** M8 - Native HTTP/1
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide private operation I/O contract and fake backend for libhttpserver v3.0.

**Action Items:**
- [ ] Define owned accept, read, write, timer, wake and cancel operations.
- [ ] Implement fake backend with one terminal completion.
- [ ] Serialize reordered completions through a connection owner.

**Dependencies:**
- Blocked by: TASK-098
- Blocks: TASK-100, TASK-101, TASK-124, TASK-126, TASK-127, TASK-130, TASK-146, TASK-149

**Acceptance Criteria:**
- Accept/read/write/timer/cancel complete exactly once under reordered fake completions.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014, PRD-V3N-REQ-016
**Related Decisions:** DR-V3-004

**Status:** Complete
