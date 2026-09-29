### TASK-112: Implement immutable reusable response definitions and overlays

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Response and resource ownership
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide immutable reusable response definitions and overlays for libhttpserver v3.0.

**Action Items:**
- [ ] Define replayable owned bytes, reopenable file and source-factory definitions.
- [ ] Allocate fresh body cursors for every send.
- [ ] Validate and append ordered per-send header and trailer overlays.

**Dependencies:**
- Blocked by: TASK-104, TASK-107
- Blocks: TASK-113

**Acceptance Criteria:**
- Concurrent sends of one definition use independent cursors and ordered per-send fields without mutation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-026, PRD-V3N-REQ-028, PRD-V3N-REQ-029, PRD-V3N-REQ-030
**Related Decisions:** DR-V3-005

**Status:** Not Started
