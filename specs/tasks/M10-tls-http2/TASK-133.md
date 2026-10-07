### TASK-133: Implement initial-handshake mTLS profiles and peer metadata

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide initial-handshake mTLS profiles and peer metadata for libhttpserver v3.0.

**Action Items:**
- [x] Implement none, request and require client-certificate modes.
- [x] Validate trust chain and copy library-owned peer metadata.
- [x] Reject post-handshake client-auth policy on QUIC.

**Dependencies:**
- Blocked by: TASK-131, TASK-132
- Blocks: TASK-147

**Acceptance Criteria:**
- None/request/require modes enforce trust policy and QUIC post-handshake auth is unavailable.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-034
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Complete
