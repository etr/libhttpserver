### TASK-178: Gate full HTTP/3 and WebSocket-over-H3 conformance

**Milestone:** M12 - Full HTTP/3
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide full HTTP/3 and WebSocket-over-H3 conformance for libhttpserver v3.0.

**Action Items:**
- [ ] Aggregate RFC 9114/9204/9220 transcripts and independent clients.
- [ ] Gate QUIC/H3/WS malformed-input and slow-peer tests.
- [ ] Publish exact tool versions, exclusions and artifacts.

**Dependencies:**
- Blocked by: TASK-173, TASK-174, TASK-175, TASK-176, TASK-177
- Blocks: TASK-179, TASK-180, TASK-181, TASK-182

**Acceptance Criteria:**
- All applicable RFC 9114/9204/9220, interop, fuzz and bounded-memory gates pass before release.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-011, PRD-V3N-REQ-012, PRD-V3N-REQ-013
**Related Decisions:** DR-V3-001

**Status:** Not Started
