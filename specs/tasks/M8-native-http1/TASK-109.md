### TASK-109: Implement Expect admission, early rejection and suspension deadlines

**Milestone:** M8 - Native HTTP/1
**Component:** Exchange and body streaming
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide Expect admission, early rejection and suspension deadlines for libhttpserver v3.0.

**Action Items:**
- [ ] Emit 100 Continue only after body admission.
- [ ] Bound early body bytes and apply drain-or-close on rejection.
- [ ] Apply suspension timeout and cancellation before admission.

**Dependencies:**
- Blocked by: TASK-102, TASK-106, TASK-108
- Blocks: None

**Acceptance Criteria:**
- 100 Continue follows admission only; early upload bytes remain bounded; suspended work times out once.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-023, PRD-V3N-REQ-024, PRD-V3N-REQ-025
**Related Decisions:** DR-V3-003

**Status:** Complete
