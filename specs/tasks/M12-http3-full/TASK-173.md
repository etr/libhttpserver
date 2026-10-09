### TASK-173: Run required QUIC interop-runner matrix

**Milestone:** M12 - Full HTTP/3
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide required QUIC interop-runner matrix for libhttpserver v3.0.

**Action Items:**
- [x] Run interop-runner handshake, loss, rebinding, amplification and key-update cases.
- [x] Use at least two independent peer stacks.
- [x] Record versions, exclusions and failure artifacts.

**Dependencies:**
- Blocked by: TASK-163, TASK-164, TASK-165, TASK-166
- Blocks: TASK-178

**Acceptance Criteria:**
- Two independent stacks pass handshake, loss, rebinding, amplification, key-update and closure cases.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001

**Status:** In Progress

**Implementation evidence:** [Required matrix and local checks](../../../docs/task-173-quic-interop-evidence.md). Validation and final completion status remain coordinator-owned.
