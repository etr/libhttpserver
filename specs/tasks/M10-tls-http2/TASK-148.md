### TASK-148: Run TLS-on/off installed-consumer dependency audit

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide TLS-on/off installed-consumer dependency audit for libhttpserver v3.0.

**Pre-cutover scope:**
TASK-148 audits the actual transitional installed aggregate package in both native
TLS modes. Its legacy dependencies must be explicitly declared and traced to
the legacy build/provider graph; unexplained dependencies fail. Native-only
artifacts retain the stricter platform/C++ runtime plus optional OpenSSL policy,
with no MHD/GnuTLS allowance. Public installed headers must remain backend-free.
This audit does not certify the aggregate package as dependency-free v3.
[TASK-184](../M13-v3-release/TASK-184.md) owns removal of legacy build/link/package
paths and the ABI cutover; TASK-185 owns final release gates. Preserve those
requirements and supply a strict post-cutover audit mode, rather than moving
their implementation into M10 or treating known legacy linkage as a plan blocker.

**Action Items:**
- [x] Install TLS-on and TLS-off packages into clean consumer environments.
- [x] Inspect public headers and direct/transitive dynamic dependencies.
- [x] Run smoke traffic and fail on undeclared MHD/GnuTLS linkage.

**Dependencies:**
- Blocked by: TASK-129, TASK-145, TASK-146, TASK-147
- Blocks: None

**Acceptance Criteria:**
- Installed headers and binaries satisfy the explicit pre-cutover policy above; all direct/transitive dependencies are inventoried, and undeclared linkage fails. The strict post-cutover policy rejects legacy dependencies without exceptions.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** In Progress


**Implementation evidence:** [Installed consumer receipts](../../../docs/task-148-installed-consumer-evidence.md).
Fresh C++20 TLS-on/off packages, shared/static installed consumers and bounded
plaintext traffic pass the declared pre-cutover audit. All installed headers
are inventoried and backend-free; strict mode rejects current legacy linkage.
TASK-148 remains In Progress pending caller-owned validation/finalization.
BSD, Windows and other nonlocal checks remain assigned to CI and the v3 PR.
