### TASK-116: Port URL-encoded form handling with bounded admission

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Authentication and forms
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide URL-encoded form handling with bounded admission for libhttpserver v3.0.

**Action Items:**
- [ ] Parse URL-encoded forms incrementally under configured limits.
- [ ] Preserve documented repeated-field behavior.
- [ ] Reject malformed escapes and oversized data before unbounded storage.

**Dependencies:**
- Blocked by: TASK-103, TASK-106
- Blocks: TASK-117

**Acceptance Criteria:**
- Form fields match v2 semantics within cap and malformed/oversize forms reject before unbounded buffering.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-021, PRD-V3N-REQ-022, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Not Started
