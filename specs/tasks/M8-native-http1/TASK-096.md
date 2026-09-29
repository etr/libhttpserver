### TASK-096: Capture v2 observable-behavior baseline and native transcript harness

**Milestone:** M8 - Native HTTP/1
**Component:** Build/packaging/validation
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide v2 observable-behavior baseline and native transcript harness for libhttpserver v3.0.

**Action Items:**
- [ ] Inventory documented v2 route, hook, auth, form, file, IP, TLS, WebSocket and SHOUTcast behavior.
- [ ] Capture fixture requests and expected observable outputs.
- [ ] Build deterministic segmented-input transcript runner.

**Dependencies:**
- Blocked by: None
- Blocks: TASK-097

**Acceptance Criteria:**
- Version-scoped parity inventory and segmented-input transcript runner cover documented v2 behavior without MHD-specific assertions.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Not Started
