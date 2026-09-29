### TASK-097: Define public semantic types and ordered fields

**Milestone:** M8 - Native HTTP/1
**Component:** Public semantic HTTP API
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide public semantic types and ordered fields for libhttpserver v3.0.

**Action Items:**
- [ ] Define owned method, protocol and typed outcome values, including extension methods.
- [ ] Implement insertion-ordered multivalue fields with append/replace/first/all.
- [ ] Keep raw target distinct from validated route path.

**Dependencies:**
- Blocked by: TASK-096
- Blocks: TASK-098, TASK-101, TASK-105, TASK-137, TASK-159

**Acceptance Criteria:**
- Append, replace, first and all preserve or update repeated fields as documented; raw target and route path differ explicitly.
- Extension methods round-trip through the public API; installed TLS-on/off headers compile identically and expose no OS socket or backend enum types.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-017, PRD-V3N-REQ-018, PRD-V3N-REQ-019, PRD-V3N-REQ-020, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-001

**Status:** Not Started
