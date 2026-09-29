### TASK-171: Implement HTTP/3 WebSocket Extended CONNECT negotiation

**Milestone:** M12 - Full HTTP/3
**Component:** WebSocket codec and adapters
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/3 WebSocket Extended CONNECT negotiation for libhttpserver v3.0.

**Action Items:**
- [ ] Require peer SETTINGS_ENABLE_CONNECT_PROTOCOL.
- [ ] Validate CONNECT pseudo-headers and registered WS route.
- [ ] Reject unsupported protocols and premature CONNECT.

**Dependencies:**
- Blocked by: TASK-121, TASK-161, TASK-169
- Blocks: TASK-172

**Acceptance Criteria:**
- Only peer-enabled CONNECT on a registered route upgrades the selected request stream; extension offers receive no negotiation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-011, PRD-V3N-REQ-012
**Related Decisions:** DR-V3-001, DR-V3-003

**Status:** Not Started
