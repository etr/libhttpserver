### TASK-107: Implement HTTP/1 response framing and ordered persistence

**Milestone:** M8 - Native HTTP/1
**Component:** HTTP/1 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/1 response framing and ordered persistence for libhttpserver v3.0.

**Action Items:**
- [ ] Serialize HTTP/1 status, fields, content, trailers and no-body cases.
- [ ] Maintain keepalive and pipelined response order.
- [ ] Bound later queued responses behind a slow earlier response.

**Dependencies:**
- Blocked by: TASK-104, TASK-105
- Blocks: TASK-108, TASK-110, TASK-112

**Acceptance Criteria:**
- HEAD/no-content rules, trailers, keepalive and pipelined response order pass transcript tests.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-004, PRD-V3N-REQ-026, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete
