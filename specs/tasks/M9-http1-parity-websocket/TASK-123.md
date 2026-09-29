### TASK-123: Integrate WebSocket close with cancellation and server drain

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** WebSocket codec and adapters
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide WebSocket close with cancellation and server drain for libhttpserver v3.0.

**Action Items:**
- [ ] Coordinate WebSocket Close with server drain deadline.
- [ ] Wake blocked send/receive on cancellation.
- [ ] Report one reason without affecting unrelated connections.

**Dependencies:**
- Blocked by: TASK-110, TASK-121, TASK-122
- Blocks: TASK-128

**Acceptance Criteria:**
- Drain sends Close, waits to deadline, then cancels once with a recorded reason.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-013, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-033
**Related Decisions:** DR-V3-008

**Status:** Not Started
