### TASK-108: Wire native TCP listener to an end-to-end routed HTTP/1 service

**Milestone:** M8 - Native HTTP/1
**Component:** HTTP/1 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide native TCP listener to an end-to-end routed HTTP/1 service for libhttpserver v3.0.

**Action Items:**
- [ ] Bind TCP listener, HTTP/1 codec and exchange route.
- [ ] Exercise GET and POST through independent HTTP/1.0 and 1.1 clients.
- [ ] Build and audit the native TLS-off target with only platform and C++ runtime dependencies.

**Dependencies:**
- Blocked by: TASK-100, TASK-102, TASK-106, TASK-107
- Blocks: TASK-109, TASK-110, TASK-111, TASK-114, TASK-118, TASK-119, TASK-120, TASK-122, TASK-129, TASK-181

**Acceptance Criteria:**
- Independent HTTP/1.0 and 1.1 clients complete GET and POST; the native TLS-off target has no MHD or other third-party runtime linkage.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-002, PRD-V3N-REQ-004, PRD-V3N-REQ-009
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete
