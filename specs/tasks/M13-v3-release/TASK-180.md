### TASK-180: Verify hierarchical resource limits and slow-peer plateaus across engines

**Milestone:** M13 - v3.0 release
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide hierarchical resource limits and slow-peer plateaus across engines for libhttpserver v3.0.

**Action Items:**
- [ ] Stress header, body, connection, stream and WS budgets on all engines.
- [ ] Measure memory/CPU plateaus with slow peers.
- [ ] Verify reservation release and control-plane progress after cancellation.

**Dependencies:**
- Blocked by: TASK-128, TASK-145, TASK-178
- Blocks: TASK-185

**Acceptance Criteria:**
- Configured header/body/connection/stream/WebSocket limits reject excess without unbounded memory or CPU.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014, PRD-V3N-REQ-016, PRD-V3N-REQ-021, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001

**Status:** Not Started
