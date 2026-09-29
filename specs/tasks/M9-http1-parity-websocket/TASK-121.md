### TASK-121: Implement transport-neutral WebSocket frame codec and session

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** WebSocket codec and adapters
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide transport-neutral WebSocket frame codec and session for libhttpserver v3.0.

**Action Items:**
- [ ] Implement RFC 6455 framing, masking and fragmentation state.
- [ ] Track incremental UTF-8, ping/pong, close and message limits.
- [ ] Expose try_send results for accepted, backpressured and closed sends, plus exactly-once close notification.

**Dependencies:**
- Blocked by: TASK-098, TASK-104
- Blocks: TASK-122, TASK-123, TASK-144, TASK-171

**Acceptance Criteria:**
- Masking, fragmentation, UTF-8, control frames, message caps and exactly-once close pass direct codec tests; unsupported RSV bits are rejected.
- try_send returns accepted, backpressured and closed outcomes, including a send after close.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-012, PRD-V3N-REQ-013
**Related Decisions:** DR-V3-001, DR-V3-003

**Status:** Not Started
