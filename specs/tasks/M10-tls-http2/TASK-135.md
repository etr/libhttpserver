### TASK-135: Implement TLS 1.2 and 1.3 external-PSK profiles

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide TLS 1.2 and 1.3 external-PSK profiles for libhttpserver v3.0.

**Action Items:**
- [ ] Map provider lookup to TLS 1.2 and TLS 1.3 external-PSK callbacks.
- [ ] Separate PSK from incompatible certificate/mTLS profiles.
- [ ] Test concurrent identities, unknown keys and 0-RTT rejection.

**Dependencies:**
- Blocked by: TASK-131, TASK-132, TASK-134
- Blocks: TASK-147

**Acceptance Criteria:**
- Independent clients authenticate or fail by identity, key material is zeroed and incompatible profiles reject pre-listen.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-034
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Not Started
