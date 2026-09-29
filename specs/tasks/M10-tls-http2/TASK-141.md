### TASK-141: Implement HTTP/2 streaming bodies and two-level flow control

**Milestone:** M10 - TLS and HTTP/2
**Component:** HTTP/2 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/2 streaming bodies and two-level flow control for libhttpserver v3.0.

**Action Items:**
- [ ] Track independent stream and connection receive/send windows.
- [ ] Release receive credit as application reads.
- [ ] Keep control frames processable under blocked request data.

**Dependencies:**
- Blocked by: TASK-103, TASK-104, TASK-140
- Blocks: TASK-142, TASK-144, TASK-145

**Acceptance Criteria:**
- Two streams progress independently; WINDOW_UPDATE follows consumption and control frames progress under data stalls.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-006, PRD-V3N-REQ-021, PRD-V3N-REQ-025, PRD-V3N-REQ-026, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
