### TASK-134: Resolve external-PSK lookup and timeout execution contract

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide external-PSK lookup and timeout execution contract for libhttpserver v3.0.

**Action Items:**
- [ ] Investigate OpenSSL PSK callback suspension limits.
- [ ] Choose deadline-safe lookup execution outside the I/O owner.
- [ ] Document provider-neutral callback, key bounds, cancellation and zeroization.

**Dependencies:**
- Blocked by: TASK-129, TASK-131
- Blocks: TASK-135

**Acceptance Criteria:**
- A documented provider-neutral lookup avoids blocking the I/O owner and defines cancellation, concurrency and key lifetime.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-034
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Not Started
