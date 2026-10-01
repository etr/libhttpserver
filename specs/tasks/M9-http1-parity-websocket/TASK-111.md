### TASK-111: Add bounded synchronous value-returning route adapter

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Exchange and body streaming
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide bounded synchronous value-returning route adapter for libhttpserver v3.0.

**Action Items:**
- [ ] Adapt value-returning handlers into the canonical exchange.
- [ ] Auto-admit and buffer only within declared body cap.
- [ ] Document restricted behavior versus streaming routes.

**Dependencies:**
- Blocked by: TASK-102, TASK-103, TASK-104, TASK-108
- Blocks: None

**Acceptance Criteria:**
- Small route serves a request without coroutine code and rejects a body above its configured buffer cap.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-009, PRD-V3N-REQ-021, PRD-V3N-REQ-022
**Related Decisions:** DR-V3-003

**Status:** Complete
