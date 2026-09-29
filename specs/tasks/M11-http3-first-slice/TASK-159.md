### TASK-159: Implement bounded static-only QPACK codec

**Milestone:** M11 - First HTTP/3 slice
**Component:** QPACK
**Estimate:** S
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide bounded static-only QPACK codec for libhttpserver v3.0.

**Action Items:**
- [ ] Implement QPACK static table, literals and bounded prefix integers.
- [ ] Decode/encode without dynamic references.
- [ ] Replay RFC 9204 vectors and expanded-field-limit failures.

**Dependencies:**
- Blocked by: TASK-097
- Blocks: TASK-160, TASK-167

**Acceptance Criteria:**
- RFC 9204 static/literal vectors pass with decoded-field limits and zero dynamic capacity.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-017
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Not Started
