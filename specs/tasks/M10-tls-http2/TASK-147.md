### TASK-147: Verify certificate rotation, SNI, mTLS, PSK and ACME under concurrency

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide certificate rotation, SNI, mTLS, PSK and ACME under concurrency for libhttpserver v3.0.

**Action Items:**
- [x] Use handshake barriers to test old/new certificate generations.
- [x] Exercise SNI isolation, mTLS modes, PSK identities and ACME selection.
- [x] Assert diagnostics redact secrets and failed updates preserve old state.

**Dependencies:**
- Blocked by: TASK-133, TASK-135, TASK-136, TASK-146
- Blocks: TASK-148

**Acceptance Criteria:**
- Old/new handshake barriers and hostile profile combinations pass TCP TLS tests with redacted diagnostics.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-034, PRD-V3N-REQ-035, PRD-V3N-REQ-036
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Complete

**Implementation evidence:**
- Added real TCP barrier, rotation, lifetime and hostile-profile suites for TLS 1.2/1.3.
- Local C++20 TLS-on/off builds, focused suites (16/16 on, 4/4 off), native linkage,
  check-local, changed-file cpplint and ASan/UBSan passed.
- [Requirement mapping and execution receipts](../../../docs/task-147-tls-concurrency-evidence.md).
- Nonlocal platforms remain assigned to CI and the v3 PR; no public TCP-443,
  QUIC, TSan or leak-detection proof is claimed. Caller owns validation/finalization.
