### TASK-182: Complete four-platform managed/external-loop package validation

**Milestone:** M13 - v3.0 release
**Component:** Build and packaging
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide four-platform managed/external-loop package validation for libhttpserver v3.0.

**Action Items:**
- [x] Provide install/test jobs for Linux, FreeBSD, macOS and MinGW64 packages in both TLS modes; nonlocal runtime receipts are assigned to CI / v3 PR.
- [x] Implement managed-backend and readiness external-loop exercises for every supported platform, including Windows readiness alongside managed IOCP.
- [x] Implement platform audits of dynamic and static consumer dependencies.

**Dependencies:**
- Blocked by: TASK-125, TASK-126, TASK-127, TASK-146, TASK-178
- Blocks: TASK-184, TASK-185

**Acceptance Criteria:**
- Linux, FreeBSD, macOS and MinGW64 installed packages run applicable HTTP and TLS suites in both modes, with identical configuration cases across backends and external-loop readiness tests on each platform.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-015
**Related Decisions:** DR-V3-001

**Status:** Complete

**Implementation evidence:** `docs/task-182-package-validation-evidence.md` records the local gates and the nonlocal execution boundary. Local validation and finalization are complete; nonlocal runtime checks remain CI/v3-PR owned under AGENTS.md.
