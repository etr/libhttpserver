### TASK-179: Audit v2-to-v3 behavior parity across all protocols

**Milestone:** M13 - v3.0 release
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide v2-to-v3 behavior parity across all protocols for libhttpserver v3.0.

**Action Items:**
- [x] Run executable v2 behavior corpus over all v3 protocol modes.
- [x] Classify each deviation as defect or explicit migration change.
- [x] Verify every PRD parity capability has a recorded outcome.

**Implementation evidence:** [Complete six-protocol audit and migration outcomes](../../../docs/task-179-behavior-parity.md). All 29 golden cases execute through applicable native TCP/TLS/QUIC owners (168 observations); the inventory records 60 capabilities and 360 protocol outcomes. Local C++20 builds, focused checks, receipt contracts and distribution pass. Public TLS/H2/H3 listener integration remains an explicit release gap; nonlocal checks are CI/v3-PR owned. Implementation is ready for runner-owned validation; status remains In Progress until finalization.

**Dependencies:**
- Blocked by: TASK-128, TASK-145, TASK-178
- Blocks: TASK-183, TASK-184

**Acceptance Criteria:**
- Routing, hooks, auth, forms, files, IP, TLS, WebSocket and SHOUTcast parity corpus passes or cites migration notes.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Complete
