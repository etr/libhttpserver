### TASK-117: Port streaming multipart uploads and cleanup

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Authentication and forms
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide streaming multipart uploads and cleanup for libhttpserver v3.0.

**Action Items:**
- [x] Stream multipart parts and file uploads through bounded body reads.
- [x] Apply per-part and aggregate limits.
- [x] Clean partial resources and signal hooks once on cancellation.

**Dependencies:**
- Blocked by: TASK-103, TASK-106, TASK-116
- Blocks: TASK-128

**Acceptance Criteria:**
- Large multipart uploads remain bounded; cancellation removes partial files and delivers documented hooks.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-021, PRD-V3N-REQ-025, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Complete
