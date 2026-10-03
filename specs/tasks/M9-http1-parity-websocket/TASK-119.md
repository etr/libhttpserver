### TASK-119: Port IP controls with peer-address policy

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Routing, hooks and IP policy
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide IP controls with peer-address policy for libhttpserver v3.0.

**Action Items:**
- [x] Apply IP policy at accept and validated address change.
- [x] Define peer-address snapshot semantics for exchanges.
- [x] Test blocked, allowed and changing peer cases.

**Dependencies:**
- Blocked by: TASK-108, TASK-118
- Blocks: None

**Acceptance Criteria:**
- Blocked peers are rejected before route work and connection/address changes have a documented policy.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Complete
