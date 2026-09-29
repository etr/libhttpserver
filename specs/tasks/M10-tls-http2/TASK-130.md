### TASK-130: Drive nonblocking TCP TLS through private I/O operations

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide nonblocking TCP TLS through private I/O operations for libhttpserver v3.0.

**Action Items:**
- [ ] Drive SSL handshake, read, write and shutdown through owned I/O operations.
- [ ] Translate WANT_READ/WANT_WRITE and alerts into typed outcomes.
- [ ] Test timeout, peer close and cancellation without blocking I/O workers.

**Dependencies:**
- Blocked by: TASK-099, TASK-129
- Blocks: TASK-131, TASK-132, TASK-139

**Acceptance Criteria:**
- Handshake, read, write, EOF, timeout and cancellation complete once without blocking I/O workers.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-034
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Not Started
