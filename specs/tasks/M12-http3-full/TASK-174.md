### TASK-174: Run independent HTTP/3 semantic client matrix

**Milestone:** M12 - Full HTTP/3
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide independent HTTP/3 semantic client matrix for libhttpserver v3.0.

**Action Items:**
- [ ] Run two independent H3 clients across concurrency, body streaming and trailers.
- [ ] Exercise cancellation, slow consumers and GOAWAY.
- [ ] Compare route semantics against H1/H2.

**Dependencies:**
- Blocked by: TASK-169, TASK-170
- Blocks: TASK-178

**Acceptance Criteria:**
- Two clients pass concurrency, streaming, trailers, cancellation, slow peers and GOAWAY.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-009
**Related Decisions:** DR-V3-001

**Status:** Not Started
