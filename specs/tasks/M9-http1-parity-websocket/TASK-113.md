### TASK-113: Implement file, pipe and borrowed-buffer response ownership

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Response and resource ownership
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide file, pipe and borrowed-buffer response ownership for libhttpserver v3.0.

**Action Items:**
- [ ] Implement owned_file and owned_pipe transfer and borrowed lifetime leases.
- [ ] Reject sharing one-shot pipe or borrowed body without valid lease.
- [ ] Test cancellation and concurrent send cleanup.

**Dependencies:**
- Blocked by: TASK-104, TASK-112
- Blocks: TASK-128

**Acceptance Criteria:**
- Owned handles close once; borrowed leases outlive send; non-replayable pipe cannot be shared.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-028, PRD-V3N-REQ-029
**Related Decisions:** DR-V3-005

**Status:** Complete
