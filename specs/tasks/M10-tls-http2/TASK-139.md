### TASK-139: Implement HTTP/2 preface, frame and SETTINGS machine

**Milestone:** M10 - TLS and HTTP/2
**Component:** HTTP/2 connection engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/2 preface, frame and SETTINGS machine for libhttpserver v3.0.

**Action Items:**
- [x] Parse preface and frame headers under size limits.
- [x] Implement SETTINGS/ACK, PING and unknown-frame behavior.
- [x] Map malformed frame sequences to typed stream/connection errors.

**Dependencies:**
- Blocked by: TASK-130, TASK-132, TASK-137
- Blocks: TASK-140, TASK-145

**Acceptance Criteria:**
- Frame segmentation, SETTINGS/ACK and control limits pass transcripts with defined connection errors.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-014, PRD-V3N-REQ-016
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress

**Implementation evidence:** [HTTP/2 evidence](../../../docs/task-139-http2-evidence.md). Private bounded framing/control implementation and local implementation checks complete; runner-owned validation and finalization pending.
