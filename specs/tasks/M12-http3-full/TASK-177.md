### TASK-177: Fuzz HTTP/3 framing, QPACK and blocked streams

**Milestone:** M12 - Full HTTP/3
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/3 framing, QPACK and blocked streams for libhttpserver v3.0.

**Action Items:**
- [ ] Generate H3 frame, control-stream and QPACK instruction permutations.
- [ ] Fuzz blocked-section cancellation and decoded-size limits.
- [ ] Assert bounded memory/work and typed errors.

**Dependencies:**
- Blocked by: TASK-169, TASK-170
- Blocks: TASK-178

**Acceptance Criteria:**
- Malformed frames and encoder/request reorder stay within memory/work budgets and return defined errors.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-017, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001

**Status:** Not Started
