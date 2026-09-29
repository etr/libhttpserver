### TASK-140: Route HTTP/2 headers-only streams through the exchange

**Milestone:** M10 - TLS and HTTP/2
**Component:** HTTP/2 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/2 headers-only streams through the exchange for libhttpserver v3.0.

**Action Items:**
- [ ] Assemble HEADERS/CONTINUATION in connection wire order.
- [ ] Validate pseudo-header order, duplication and forbidden fields.
- [ ] Bridge decoded heads to the shared route exchange.

**Dependencies:**
- Blocked by: TASK-102, TASK-138, TASK-139
- Blocks: TASK-141, TASK-144, TASK-145

**Acceptance Criteria:**
- Concurrent GETs reach the same route as HTTP/1; malformed pseudo-headers and continuation ordering fail correctly.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-009, PRD-V3N-REQ-017, PRD-V3N-REQ-020
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
