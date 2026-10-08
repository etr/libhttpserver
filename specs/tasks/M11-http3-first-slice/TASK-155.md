### TASK-155: Bridge OpenSSL QUIC TLS callbacks to owned CRYPTO streams

**Milestone:** M11 - First HTTP/3 slice
**Component:** TLS adapter and credential registry
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide OpenSSL QUIC TLS callbacks to owned CRYPTO streams for libhttpserver v3.0.

**Action Items:**
- [x] Adapt ordered CRYPTO bytes to SSL_set_quic_tls_cbs.
- [x] Own callback and transport-parameter buffer lifetimes.
- [x] Install secrets by level, select h3 ALPN and disable 0-RTT.

**Dependencies:**
- Blocked by: TASK-131, TASK-132, TASK-152, TASK-153
- Blocks: TASK-156, TASK-160, TASK-163

**Acceptance Criteria:**
- Independent client handshake negotiates h3; callback buffers live correctly; 0-RTT remains disabled.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-034
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Complete

**Implementation evidence:** [QUIC TLS bridge and local checks](../../../docs/task-155-quic-tls.md). Validation and integration remain pending.
