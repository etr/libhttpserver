### TASK-103: Implement bounded body reader and collect

**Milestone:** M8 - Native HTTP/1
**Component:** Exchange and body streaming
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide bounded body reader and collect for libhttpserver v3.0.

**Action Items:**
- [ ] Provide one outstanding incremental body read with trailer access after EOF.
- [ ] Implement collect(max) with exact over-limit outcome.
- [ ] Return receive credit only when bytes are consumed.

**Dependencies:**
- Blocked by: TASK-102
- Blocks: TASK-106, TASK-111, TASK-116, TASK-117, TASK-141, TASK-161

**Acceptance Criteria:**
- Echo input larger than the queue progresses through reads; collect fails at its declared cap; cancellation wakes reads.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-021, PRD-V3N-REQ-022, PRD-V3N-REQ-025
**Related Decisions:** DR-V3-003

**Status:** Not Started
