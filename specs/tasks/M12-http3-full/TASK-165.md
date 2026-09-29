### TASK-165: Implement QUIC datagram sizing and black-hole recovery

**Milestone:** M12 - Full HTTP/3
**Component:** QUIC v1 engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide QUIC datagram sizing and black-hole recovery for libhttpserver v3.0.

**Action Items:**
- [ ] Enforce 1200-byte Initial and active-path send limits.
- [ ] Coalesce packets conservatively and probe PMTU.
- [ ] Recover from black-holed larger datagrams in deterministic traces.

**Dependencies:**
- Blocked by: TASK-158, TASK-164
- Blocks: TASK-173

**Acceptance Criteria:**
- Initial and active-path size rules pass loss/PMTU traces and recover from black-holed larger packets.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
