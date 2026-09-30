### TASK-102: Implement header-time exchange decisions and route execution

**Milestone:** M8 - Native HTTP/1
**Component:** Exchange and body streaming
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide header-time exchange decisions and route execution for libhttpserver v3.0.

**Action Items:**
- [ ] Route complete request heads through one exchange state machine.
- [ ] Implement respond, admit, suspend and upgrade decisions before body delivery.
- [ ] Reject double terminal actions and contain handler exceptions.

**Dependencies:**
- Blocked by: TASK-098, TASK-101
- Blocks: TASK-103, TASK-104, TASK-108, TASK-109, TASK-111, TASK-114, TASK-118, TASK-140, TASK-161

**Acceptance Criteria:**
- One route can reject, admit, suspend or upgrade before body delivery; double terminal actions fail predictably.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-009, PRD-V3N-REQ-023, PRD-V3N-REQ-024, PRD-V3N-REQ-025
**Related Decisions:** DR-V3-003

**Status:** Complete
