### TASK-179: Audit v2-to-v3 behavior parity across all protocols

**Milestone:** M13 - v3.0 release
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide v2-to-v3 behavior parity across all protocols for libhttpserver v3.0.

**Action Items:**
- [ ] Run executable v2 behavior corpus over all v3 protocol modes.
- [ ] Classify each deviation as defect or explicit migration change.
- [ ] Verify every PRD parity capability has a recorded outcome.

**Dependencies:**
- Blocked by: TASK-128, TASK-145, TASK-178
- Blocks: TASK-183, TASK-184

**Acceptance Criteria:**
- Routing, hooks, auth, forms, files, IP, TLS, WebSocket and SHOUTcast parity corpus passes or cites migration notes.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Not Started
