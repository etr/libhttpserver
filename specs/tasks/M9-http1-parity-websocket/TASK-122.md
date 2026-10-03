### TASK-122: Implement WebSocket over HTTP/1.1 upgrade

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** WebSocket codec and adapters
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide WebSocket over HTTP/1.1 upgrade for libhttpserver v3.0.

**Action Items:**
- [x] Validate HTTP/1.1 Upgrade tokens, key, version, origin and subprotocol.
- [x] Hand the ordered byte stream to the shared codec.
- [x] Run independent echo and malformed-upgrade clients.

**Dependencies:**
- Blocked by: TASK-105, TASK-108, TASK-121
- Blocks: TASK-123, TASK-128

**Acceptance Criteria:**
- Independent client completes handshake, echo, invalid-upgrade rejection and backpressured send; extension offers receive no negotiation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-010, PRD-V3N-REQ-012, PRD-V3N-REQ-013
**Related Decisions:** DR-V3-001, DR-V3-003

**Status:** Complete

**Implementation evidence:** [Native upgrade design and receipts](../../architecture/v3/TASK-122-design-evidence.md).
Fresh serial tests: 216/216; focused replay: 27/27. Independent
`websockets==15.0.1` handshake, echo, malformed refusal and real socket
backpressure pass, including extension omission and ordered 64 MiB resume.
ASan/UBSan, current-tree TSan, native linkage, installed consumers,
source distribution and header/docs/install/hygiene checks pass.
Caller validation and merge remain separate; TASK-123 owns server drain.
