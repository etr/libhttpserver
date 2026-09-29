### TASK-105: Implement strict HTTP/1 start-line and header parser

**Milestone:** M8 - Native HTTP/1
**Component:** HTTP/1 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide strict HTTP/1 start-line and header parser for libhttpserver v3.0.

**Action Items:**
- [ ] Parse HTTP/1 start lines and fields incrementally as octets.
- [ ] Enforce field count and byte limits before allocation.
- [ ] Reject malformed whitespace, folding and invalid syntax with required close policy.

**Dependencies:**
- Blocked by: TASK-097, TASK-101
- Blocks: TASK-106, TASK-107, TASK-122

**Acceptance Criteria:**
- HTTP/1.0 and 1.1 heads parse under every split; malformed syntax and oversized fields reject before route delivery.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-004, PRD-V3N-REQ-017, PRD-V3N-REQ-019
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
