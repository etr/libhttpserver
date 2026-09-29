### TASK-170: Implement staged HTTP/3 GOAWAY and graceful drain

**Milestone:** M12 - Full HTTP/3
**Component:** HTTP/3 connection engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide staged HTTP/3 GOAWAY and graceful drain for libhttpserver v3.0.

**Action Items:**
- [ ] Issue staged H3 GOAWAY IDs while retaining critical streams.
- [ ] Complete accepted requests and refuse later streams.
- [ ] Close QUIC at deadline with observable drain result.

**Dependencies:**
- Blocked by: TASK-143, TASK-166, TASK-169
- Blocks: TASK-172, TASK-174, TASK-177

**Acceptance Criteria:**
- Accepted streams finish and later request IDs are refused before QUIC close at deadline.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-032
**Related Decisions:** DR-V3-008

**Status:** Not Started
