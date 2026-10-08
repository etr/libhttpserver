### TASK-160: Implement HTTP/3 control streams, SETTINGS and frame roles

**Milestone:** M11 - First HTTP/3 slice
**Component:** HTTP/3 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/3 control streams, SETTINGS and frame roles for libhttpserver v3.0.

**Action Items:**
- [x] Classify H3 request and unidirectional stream roles.
- [x] Enforce one critical control stream and SETTINGS-first.
- [x] Parse DATA/HEADERS frames with bounded lengths and typed errors.

**Dependencies:**
- Blocked by: TASK-155, TASK-157, TASK-159
- Blocks: TASK-161, TASK-168

**Acceptance Criteria:**
- Critical control streams and SETTINGS-first rules pass transcripts; malformed frames map to correct errors.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete
