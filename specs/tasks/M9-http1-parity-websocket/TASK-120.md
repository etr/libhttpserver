### TASK-120: Port SHOUTcast and remaining HTTP/1 parity cases

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Routing, hooks and IP policy
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide SHOUTcast and remaining HTTP/1 parity cases for libhttpserver v3.0.

**Action Items:**
- [x] Port SHOUTcast response semantics into HTTP/1 output.
- [x] Inventory other documented HTTP/1 edge cases.
- [x] Replay each against the v2 observable-behavior matrix.

**Dependencies:**
- Blocked by: TASK-108, TASK-118
- Blocks: TASK-128

**Acceptance Criteria:**
- An executable parity corpus covers SHOUTcast and every documented remaining HTTP/1 capability.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-004, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Complete
