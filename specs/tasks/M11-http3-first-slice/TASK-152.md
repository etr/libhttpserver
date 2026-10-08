### TASK-152: Implement QUIC packet protection and key lifecycle with OpenSSL EVP

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC packet protection and key lifecycle with OpenSSL EVP for libhttpserver v3.0.

**Action Items:**
- [x] Use OpenSSL EVP/HKDF/AEAD for Initial and later packet protection.
- [x] Implement header protection, nonce and Retry integrity.
- [x] Replay RFC 9001 packet/key vectors and zero retired secrets.

**Dependencies:**
- Blocked by: TASK-129, TASK-151
- Blocks: TASK-153, TASK-155, TASK-156

**Acceptance Criteria:**
- RFC 9001 Initial, Retry, header-protection and key-update vectors pass without OpenSSL owning transport.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-003, PRD-V3N-REQ-007
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete

**Implementation evidence:** [QUIC protection and local receipts](../../../docs/task-152-quic-protection.md). Implementation prepared for coordinator validation; status remains In Progress.
