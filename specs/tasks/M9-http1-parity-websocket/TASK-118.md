### TASK-118: Port route matching and lifecycle hook behavior

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Routing, hooks and IP policy
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide route matching and lifecycle hook behavior for libhttpserver v3.0.

**Action Items:**
- [ ] Map v2 route families to the v3 route table.
- [ ] Run lifecycle hooks at documented native-engine phases.
- [ ] Replay route and hook parity fixtures with explicit migration exceptions.

**Dependencies:**
- Blocked by: TASK-102, TASK-108
- Blocks: TASK-119, TASK-120, TASK-128

**Acceptance Criteria:**
- v2 routing and hook corpus passes through native HTTP/1 with documented v3 exceptions.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-009, PRD-V3N-REQ-023, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Not Started
