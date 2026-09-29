### TASK-142: Implement HTTP/2 fair output, resets and rate budgets

**Milestone:** M10 - TLS and HTTP/2
**Component:** HTTP/2 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/2 fair output, resets and rate budgets for libhttpserver v3.0.

**Action Items:**
- [ ] Schedule response DATA fairly under bounded queues.
- [ ] Implement RST_STREAM and cancellation cleanup.
- [ ] Rate-limit control amplification and rapid stream churn.

**Dependencies:**
- Blocked by: TASK-141
- Blocks: TASK-143, TASK-145

**Acceptance Criteria:**
- Large responses cannot starve small ones; RST_STREAM and control-frame abuse remain bounded.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-006, PRD-V3N-REQ-025, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
