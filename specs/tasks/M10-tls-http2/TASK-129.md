### TASK-129: Establish OpenSSL 3.5 LTS build boundary and feature gates

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide OpenSSL 3.5 LTS build boundary and feature gates for libhttpserver v3.0.

**Action Items:**
- [ ] Add explicit TLS-on and TLS-off build configurations.
- [ ] Gate OpenSSL 3.5 QUIC callback API and release patch floor.
- [ ] Keep OpenSSL includes and types private while public declarations stay identical.

**Dependencies:**
- Blocked by: TASK-108
- Blocks: TASK-130, TASK-131, TASK-134, TASK-148, TASK-152

**Acceptance Criteria:**
- TLS-on builds require the selected patched 3.5 line; TLS-off has no OpenSSL symbols or headers in public API.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-005, PRD-V3N-REQ-007, PRD-V3N-REQ-034, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Not Started
