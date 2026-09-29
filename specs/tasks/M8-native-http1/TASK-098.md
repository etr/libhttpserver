### TASK-098: Define C++20 task executor, cancellation and resume signals

**Milestone:** M8 - Native HTTP/1
**Component:** Route/admission/lifecycle services
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide C++20 task executor, cancellation and resume signals for libhttpserver v3.0.

**Action Items:**
- [ ] Specify move-only task frame ownership and executor affinity.
- [ ] Implement cancellation fan-out and idempotent application resume signals.
- [ ] Exercise completion, timeout, disconnect and stop races.

**Dependencies:**
- Blocked by: TASK-097
- Blocks: TASK-099, TASK-102, TASK-110, TASK-121

**Acceptance Criteria:**
- Suspended work resumes once on event, timeout or cancellation; request_stop returns inside a handler.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-024, PRD-V3N-REQ-025, PRD-V3N-REQ-031
**Related Decisions:** DR-V3-003

**Status:** Complete
