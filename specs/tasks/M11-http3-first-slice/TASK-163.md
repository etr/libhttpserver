### TASK-163: Package a QUIC interop-runner endpoint and diagnostics

**Milestone:** M11 - First HTTP/3 slice
**Component:** Build/packaging/validation
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide a QUIC interop-runner endpoint and diagnostics for libhttpserver v3.0.

**Action Items:**
- [x] Create test-only interop-runner endpoint and container.
- [x] Export reproducible logs, packet captures and key material only in test mode.
- [x] Run handshake and transfer scenarios.

**Dependencies:**
- Blocked by: TASK-155, TASK-157, TASK-161
- Blocks: TASK-173

**Acceptance Criteria:**
- Runner handshake and transfer cases execute with reproducible packet/log artifacts.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001

**Status:** Complete

Implementation evidence: [TASK-163 receipts](../../../docs/task-163-quic-interop-evidence.md).
