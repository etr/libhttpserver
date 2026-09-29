### TASK-126: Implement Linux epoll managed I/O backend

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide Linux epoll managed I/O backend for libhttpserver v3.0.

**Action Items:**
- [ ] Map operation contract onto epoll edge/one-shot readiness.
- [ ] Drain until EAGAIN and rearm correctly.
- [ ] Compare accept/read/write/close traces with poll oracle.

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-182

**Acceptance Criteria:**
- Edge-triggered operations drain and rearm; differential scenarios match the poll oracle.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014
**Related Decisions:** DR-V3-004

**Status:** Not Started
