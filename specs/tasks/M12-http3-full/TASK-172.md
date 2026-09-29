### TASK-172: Implement WebSocket-over-HTTP/3 DATA and lifecycle adapter

**Milestone:** M12 - Full HTTP/3
**Component:** WebSocket codec and adapters
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide WebSocket-over-HTTP/3 DATA and lifecycle adapter for libhttpserver v3.0.

**Action Items:**
- [ ] Feed only H3 DATA payload bytes to the shared WS codec.
- [ ] Frame outbound WS bytes as DATA under stream credit.
- [ ] Map FIN/reset/drain to exactly-once close without harming siblings.

**Dependencies:**
- Blocked by: TASK-166, TASK-170, TASK-171
- Blocks: TASK-175

**Acceptance Criteria:**
- Fragmented DATA carries WebSocket frames; reset/drain notify once and sibling requests continue.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-011, PRD-V3N-REQ-012, PRD-V3N-REQ-013, PRD-V3N-REQ-033
**Related Decisions:** DR-V3-008

**Status:** Not Started
