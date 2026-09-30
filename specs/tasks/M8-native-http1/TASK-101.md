### TASK-101: Implement validated server options, budgets and route registration

**Milestone:** M8 - Native HTTP/1
**Component:** Server configuration and resource budgets
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide validated server options, budgets and route registration for libhttpserver v3.0.

**Action Items:**
- [ ] Define one backend-neutral configuration surface for listeners, concurrency, timeouts, budgets, TLS provider/profile selection and enabled protocols.
- [ ] Validate incompatible options before socket acceptance.
- [ ] Reserve and release hierarchical resource budgets on every terminal path.

**Dependencies:**
- Blocked by: TASK-097, TASK-099
- Blocks: TASK-102, TASK-105

**Acceptance Criteria:**
- TLS-off plus HTTP/2 or HTTP/3, unsupported TLS profiles and other invalid combinations fail before listening; route and reservation tests enforce parent and child limits.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-009, PRD-V3N-REQ-014, PRD-V3N-REQ-016
**Related Decisions:** DR-V3-001

**Status:** Complete
