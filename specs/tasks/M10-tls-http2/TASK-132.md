### TASK-132: Implement early SNI, default-host and ALPN selection

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide early SNI, default-host and ALPN selection for libhttpserver v3.0.

**Action Items:**
- [x] Select host profile during ClientHello and acknowledge accepted SNI.
- [x] Choose h2 or HTTP/1 ALPN from the selected profile.
- [x] Test missing/unknown SNI, resumption and registry replacement.

**Dependencies:**
- Blocked by: TASK-130, TASK-131
- Blocks: TASK-133, TASK-135, TASK-136, TASK-139, TASK-155

**Acceptance Criteria:**
- h2/http1 ALPN and SNI selection are deterministic across unknown hosts, resumption and concurrent rotation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-034, PRD-V3N-REQ-035
**Related Decisions:** DR-V3-002, DR-V3-007

**Implementation evidence:** [Local TLS selection, resumption and rotation receipts](../../../docs/task-132-tls-selection-evidence.md).

**Status:** In Progress
