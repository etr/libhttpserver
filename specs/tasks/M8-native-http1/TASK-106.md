### TASK-106: Implement authoritative HTTP/1 body framing and trailers

**Milestone:** M8 - Native HTTP/1
**Component:** HTTP/1 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide authoritative HTTP/1 body framing and trailers for libhttpserver v3.0.

**Action Items:**
- [ ] Compute and persist one authoritative HTTP/1 body mode.
- [ ] Decode fixed-length and chunked content plus trailers incrementally.
- [ ] Reject TE/CL ambiguity, bad lengths and malformed chunks.

**Dependencies:**
- Blocked by: TASK-103, TASK-105
- Blocks: TASK-108, TASK-109, TASK-116, TASK-117

**Acceptance Criteria:**
- Content-Length and chunked bodies stream; conflicting lengths and TE/CL reject and close correctly.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-004, PRD-V3N-REQ-017, PRD-V3N-REQ-021, PRD-V3N-REQ-023
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete
