### TASK-150: Add deterministic QUIC network, clock and fuzz harness

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide deterministic QUIC network, clock and fuzz harness for libhttpserver v3.0.

**Action Items:**
- [ ] Build simulated clock and datagram loss/reorder/duplication network.
- [ ] Assert timers, emitted packets and retained bytes.
- [ ] Seed QUIC parser and state-machine fuzz harnesses.

**Dependencies:**
- Blocked by: TASK-149
- Blocks: TASK-151

**Acceptance Criteria:**
- Scripted loss, reorder, duplication and timer traces replay byte-for-byte with resource accounting.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
