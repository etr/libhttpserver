### TASK-162: Run independent HTTP/3 client smoke tests

**Milestone:** M11 - First HTTP/3 slice
**Component:** Build/packaging/validation
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide independent HTTP/3 client smoke tests for libhttpserver v3.0.

**Action Items:**
- [ ] Run two independent HTTP/3 clients against GET and POST routes.
- [ ] Exercise basic concurrent streams and typed failures.
- [ ] Retain packet and TLS diagnostics for failures.

**Dependencies:**
- Blocked by: TASK-161
- Blocks: None

**Acceptance Criteria:**
- Two independent clients complete TLS handshake, h3 GET/POST and basic concurrent streams.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-009
**Related Decisions:** DR-V3-001

**Status:** Not Started
