### TASK-138: Implement connection-owned HPACK dynamic tables

**Milestone:** M10 - TLS and HTTP/2
**Component:** HPACK
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide connection-owned HPACK dynamic tables for libhttpserver v3.0.

**Action Items:**
- [ ] Own encoder and decoder dynamic tables per HTTP/2 connection.
- [ ] Handle capacity updates, eviction and never-indexed fields.
- [ ] Enforce compressed and expanded field-section budgets.

**Dependencies:**
- Blocked by: TASK-137
- Blocks: TASK-140, TASK-145

**Acceptance Criteria:**
- Encoder/decoder tables evolve in wire order with correct eviction, limits and ordered duplicate fields.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-017, PRD-V3N-REQ-018
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
