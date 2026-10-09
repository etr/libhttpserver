### TASK-175: Build independent WebSocket-over-HTTP/3 client harness

**Milestone:** M12 - Full HTTP/3
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide independent WebSocket-over-HTTP/3 client harness for libhttpserver v3.0.

**Action Items:**
- [x] Build an external RFC 9220 test client or harness.
- [x] Cover handshake, masking, fragmentation, ping/pong and close.
- [x] Hold a sibling HTTP/3 request while WS resets and backpressures.

**Dependencies:**
- Blocked by: TASK-172
- Blocks: TASK-178

**Acceptance Criteria:**
- External client covers handshake, masking, fragmentation, close, reset, backpressure and sibling streams.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-011, PRD-V3N-REQ-012, PRD-V3N-REQ-013
**Related Decisions:** DR-V3-001

**Status:** Complete

**Implementation evidence:** Independent pinned aioquic RFC 9220 harness,
shared bounded fixture routes, native/runner/wire tests, and reproduction
instructions in `docs/task-175-http3-websocket-harness.md`. Linux TLS-on/off
C++20 focused suites and the real UDP matrix pass. Peer STOP_SENDING `0x10c`
now preserves the cancellation reason regardless of companion RESET_STREAM
ordering, covered by a native regression. Caller-owned validation/finalization
remain pending; nonlocal checks belong to CI/the v3 PR.
