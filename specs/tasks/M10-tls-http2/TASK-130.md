### TASK-130: Drive nonblocking TCP TLS through private I/O operations

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide nonblocking TCP TLS through private I/O operations for libhttpserver v3.0.

**Action Items:**
- [x] Drive SSL handshake, read, write and shutdown through owned I/O operations.
- [x] Translate WANT_READ/WANT_WRITE and alerts into typed outcomes.
- [x] Test timeout, peer close and cancellation without blocking I/O workers.

**Dependencies:**
- Blocked by: TASK-099, TASK-129
- Blocks: TASK-131, TASK-132, TASK-139

**Acceptance Criteria:**
- Handshake, read, write, EOF, timeout and cancellation complete once without blocking I/O workers.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-034
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Complete


**Prepared implementation evidence:** [Private adapter and local checks](../../../docs/task-130-tls-io-evidence.md).
TLS-on/off C++20 builds, 10/10 and 6/6 focused executable targets, native linkage
and repository `check-local` passed. The new suites cover 23 tests / 442 checks;
local ASan/UBSan and TSan checks also passed. Changed-file lint passes; the
repository-wide complexity gate has three independently reproduced unchanged
v3 baseline violations outside this task.

Native listener rejection and `tcp_tls=false` remain in place pending a usable
credential/configuration path. Linux/epoll, nonlocal BSD, Windows, and other
unavailable platform lanes are unexecuted and assigned to CI/the v3 PR.
Implementation is prepared for runner-owned validation and commits; status remains
In Progress until the caller completes those phases.
