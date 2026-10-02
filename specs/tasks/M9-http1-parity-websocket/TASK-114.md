### TASK-114: Port Basic authentication and in-tree hash/entropy primitives

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Authentication and forms
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide Basic authentication and in-tree hash/entropy primitives for libhttpserver v3.0.

**Action Items:**
- [ ] Port Basic auth policy above semantic request heads.
- [ ] Implement required in-tree hash helpers for TLS-off WebSocket/Digest and OS entropy use.
- [ ] Verify vectors and credential redaction.

**Dependencies:**
- Blocked by: TASK-102, TASK-108
- Blocks: TASK-115

**Acceptance Criteria:**
- TLS-off Basic and documented hash primitives pass vectors, OS entropy tests and v2 behavior cases.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-002, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Complete
