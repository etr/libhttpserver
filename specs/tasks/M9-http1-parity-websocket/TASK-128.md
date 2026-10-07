### TASK-128: Gate HTTP/1 and WebSocket conformance, fuzzing and parity

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/1 and WebSocket conformance, fuzzing and parity for libhttpserver v3.0.

**Action Items:**
- [x] Build RFC 9112 malformed/smuggling corpus and parser fuzz targets.
- [x] Run applicable WebSocket codec conformance and independent clients.
- [x] Gate slow-peer memory plateaus and v2 HTTP/1 parity under sanitizers.

**Dependencies:**
- Blocked by: TASK-113, TASK-115, TASK-117, TASK-118, TASK-120, TASK-122, TASK-123
- Blocks: TASK-179, TASK-180

**Acceptance Criteria:**
- RFC 9112 corpus, applicable WebSocket codec cases, slow-peer plateaus and sanitizer tests pass.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-004, PRD-V3N-REQ-010, PRD-V3N-REQ-012, PRD-V3N-REQ-013, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** In Progress
