### TASK-166: Implement QUIC idle, close, drain and key disposal

**Milestone:** M12 - Full HTTP/3
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC idle, close, drain and key disposal for libhttpserver v3.0.

**Action Items:**
- [ ] Implement idle, CONNECTION_CLOSE, stateless reset and draining timers.
- [ ] Retain CID lookup for required close period.
- [ ] Cancel application work and dispose keys once.

**Dependencies:**
- Blocked by: TASK-110, TASK-156, TASK-157, TASK-164
- Blocks: TASK-170, TASK-172, TASK-173, TASK-176

**Acceptance Criteria:**
- Close notifies once, CID draining state persists for protocol timers and callbacks cease after teardown.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-032
**Related Decisions:** DR-V3-008

**Status:** Not Started
