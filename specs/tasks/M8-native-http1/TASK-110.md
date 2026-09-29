### TASK-110: Implement handler-safe stop and deadline drain

**Milestone:** M8 - Native HTTP/1
**Component:** Server lifecycle
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide handler-safe stop and deadline drain for libhttpserver v3.0.

**Action Items:**
- [ ] Split nonblocking stop request from deadline-bound drain ticket.
- [ ] Apply HTTP/1 close behavior to active and pipelined work.
- [ ] Reject drain waiting from work counted by its own drain.

**Dependencies:**
- Blocked by: TASK-098, TASK-107, TASK-108
- Blocks: TASK-123, TASK-143, TASK-166

**Acceptance Criteria:**
- Handler stop is nonblocking and external drain reports completion or deadline expiry.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-031, PRD-V3N-REQ-032
**Related Decisions:** DR-V3-008

**Status:** Not Started
