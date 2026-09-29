### TASK-161: Bridge HTTP/3 request streams to semantic exchanges

**Milestone:** M11 - First HTTP/3 slice
**Component:** HTTP/3 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/3 request streams to semantic exchanges for libhttpserver v3.0.

**Action Items:**
- [ ] Validate H3 pseudo-headers and map DATA/trailers to exchange.
- [ ] Serialize responses through QUIC stream credit.
- [ ] Test equivalent HTTP/1, HTTP/2 and H3 route behavior under cancellation.

**Dependencies:**
- Blocked by: TASK-102, TASK-103, TASK-104, TASK-158, TASK-160
- Blocks: TASK-162, TASK-163, TASK-169, TASK-171

**Acceptance Criteria:**
- Equivalent GET/POST routes work over h3 with bounded bodies, trailers and cancellation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-009, PRD-V3N-REQ-021, PRD-V3N-REQ-026
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
