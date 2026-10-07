### TASK-131: Build and atomically publish immutable TLS credential snapshots

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Build and atomically publish immutable TLS credential snapshots for libhttpserver v3.0.

**Action Items:**
- [x] Validate certificate/key, trust roots, ALPN and host profiles off path.
- [x] Publish immutable SSL_CTX registry generations atomically.
- [x] Retain old generations for selected handshakes and established sessions.

**Dependencies:**
- Blocked by: TASK-129, TASK-130
- Blocks: TASK-132, TASK-133, TASK-134, TASK-135, TASK-136, TASK-155

**Acceptance Criteria:**
- Invalid replacement leaves old generation active; new handshakes select new credentials while established sessions persist.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-034, PRD-V3N-REQ-035
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Complete
