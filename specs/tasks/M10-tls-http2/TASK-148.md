### TASK-148: Run TLS-on/off installed-consumer dependency audit

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide TLS-on/off installed-consumer dependency audit for libhttpserver v3.0.

**Action Items:**
- [ ] Install TLS-on and TLS-off packages into clean consumer environments.
- [ ] Inspect public headers and direct/transitive dynamic dependencies.
- [ ] Run smoke traffic and fail on undeclared MHD/GnuTLS linkage.

**Dependencies:**
- Blocked by: TASK-129, TASK-145, TASK-146, TASK-147
- Blocks: None

**Acceptance Criteria:**
- Installed headers and binaries have only allowed third-party dependencies on supported platform builds.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Not Started
