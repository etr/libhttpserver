### TASK-146: Implement Windows IOCP managed I/O backend

**Milestone:** M10 - TLS and HTTP/2
**Component:** Private operation I/O
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide Windows IOCP managed I/O backend for libhttpserver v3.0.

**Action Items:**
- [ ] Post AcceptEx, WSARecv and WSASend with owned OVERLAPPED storage.
- [ ] Keep operations alive through completion or cancellation packets.
- [ ] Run poll-oracle and out-of-order completion traces on Windows.

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-147, TASK-148, TASK-182

**Acceptance Criteria:**
- OVERLAPPED storage survives completion/cancel races; oracle scenarios pass on native Windows.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014
**Related Decisions:** DR-V3-004

**Status:** Not Started
