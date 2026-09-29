### TASK-183: Publish v2-to-v3 migration guide and API examples

**Milestone:** M13 - v3.0 release
**Component:** Documentation and migration
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide v2-to-v3 migration guide and API examples for libhttpserver v3.0.

**Action Items:**
- [ ] Write v2-to-v3 API/behavior migration mapping.
- [ ] Update README, examples and generated API docs.
- [ ] Compile examples for sync/coroutine routes, three protocols, WS and TLS rotation.

**Dependencies:**
- Blocked by: TASK-179
- Blocks: TASK-184, TASK-185

**Acceptance Criteria:**
- Examples cover sync/coroutine routes, three protocols, WS, cert rotation and recorded behavior changes.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-009, PRD-V3N-REQ-031, PRD-V3N-REQ-037, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Not Started
