### TASK-156: Implement ACK generation, RFC 9002 loss detection and PTO

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide ACK generation, RFC 9002 loss detection and PTO for libhttpserver v3.0.

**Action Items:**
- [ ] Track sent information and ACK ranges per packet-number space.
- [ ] Implement RTT, loss thresholds and PTO timers.
- [ ] Repacketize information after loss rather than replay encrypted packets.

**Dependencies:**
- Blocked by: TASK-151, TASK-152, TASK-155
- Blocks: TASK-157, TASK-158, TASK-164, TASK-166

**Acceptance Criteria:**
- Three packet-number spaces handle loss/reorder independently and retransmit information in new packets.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
