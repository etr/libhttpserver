### TASK-122: Implement WebSocket over HTTP/1.1 upgrade

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** WebSocket codec and adapters
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide WebSocket over HTTP/1.1 upgrade for libhttpserver v3.0.

**Action Items:**
- [ ] Validate HTTP/1.1 Upgrade tokens, key, version, origin and subprotocol.
- [ ] Hand the ordered byte stream to the shared codec.
- [ ] Run independent echo and malformed-upgrade clients.

**Dependencies:**
- Blocked by: TASK-105, TASK-108, TASK-121
- Blocks: TASK-123, TASK-128

**Acceptance Criteria:**
- Independent client completes handshake, echo, invalid-upgrade rejection and backpressured send; extension offers receive no negotiation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-010, PRD-V3N-REQ-012, PRD-V3N-REQ-013
**Related Decisions:** DR-V3-001, DR-V3-003

**Status:** Not Started
