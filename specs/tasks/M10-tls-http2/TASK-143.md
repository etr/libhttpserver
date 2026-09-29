### TASK-143: Implement HTTP/2 staged GOAWAY and deadline drain

**Milestone:** M10 - TLS and HTTP/2
**Component:** HTTP/2 connection engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/2 staged GOAWAY and deadline drain for libhttpserver v3.0.

**Action Items:**
- [ ] Send staged GOAWAY and preserve accepted-stream work.
- [ ] Refuse later streams with correct identifiers.
- [ ] Report deadline completion or cancellation through drain ticket.

**Dependencies:**
- Blocked by: TASK-110, TASK-142
- Blocks: TASK-144, TASK-145, TASK-170

**Acceptance Criteria:**
- Accepted streams finish; later streams are refused with correct GOAWAY identifiers and deadline result.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-032
**Related Decisions:** DR-V3-008

**Status:** Not Started
