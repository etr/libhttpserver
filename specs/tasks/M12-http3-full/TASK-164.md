### TASK-164: Implement QUIC CID lifecycle, path validation and rebinding

**Milestone:** M12 - Full HTTP/3
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC CID lifecycle, path validation and rebinding for libhttpserver v3.0.

**Action Items:**
- [ ] Issue/retire connection IDs with bounded lookup state.
- [ ] Validate new paths before redirecting application output.
- [ ] Reevaluate IP policy and document request peer-address snapshots after rebinding.

**Dependencies:**
- Blocked by: TASK-153, TASK-156, TASK-158
- Blocks: TASK-165, TASK-166, TASK-173

**Acceptance Criteria:**
- Validated address changes retain streams while spoofed paths cannot redirect data; IP policy re-evaluates explicitly.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
