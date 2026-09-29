### TASK-144: Implement WebSocket over HTTP/2 Extended CONNECT

**Milestone:** M10 - TLS and HTTP/2
**Component:** WebSocket codec and adapters
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide WebSocket over HTTP/2 Extended CONNECT for libhttpserver v3.0.

**Action Items:**
- [ ] Advertise and require SETTINGS_ENABLE_CONNECT_PROTOCOL.
- [ ] Validate Extended CONNECT without HTTP/1 Upgrade fields.
- [ ] Map one HTTP/2 stream to the shared WebSocket codec.

**Dependencies:**
- Blocked by: TASK-121, TASK-140, TASK-141, TASK-143
- Blocks: TASK-145

**Acceptance Criteria:**
- Negotiated CONNECT creates one stream-local WebSocket while sibling HTTP/2 requests continue; extension offers receive no negotiation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-011, PRD-V3N-REQ-012, PRD-V3N-REQ-013, PRD-V3N-REQ-033
**Related Decisions:** DR-V3-001, DR-V3-003

**Status:** Not Started
