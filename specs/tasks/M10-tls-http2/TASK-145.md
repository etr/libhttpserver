### TASK-145: Gate HTTP/2 conformance, fuzzing and independent clients

**Milestone:** M10 - TLS and HTTP/2
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/2 conformance, fuzzing and independent clients for libhttpserver v3.0.

**Action Items:**
- [ ] Build RFC 9113 malformed transcript and HPACK fuzz suites.
- [ ] Run h2spec as supplemental evidence with explicit RFC 7540 exclusions.
- [ ] Test two independent clients for multiplexing, resets, flow and WS.

**Dependencies:**
- Blocked by: TASK-138, TASK-139, TASK-140, TASK-141, TASK-142, TASK-143, TASK-144
- Blocks: TASK-148, TASK-179, TASK-180, TASK-181

**Acceptance Criteria:**
- RFC 9113 tests, supplemental h2spec, malformed seeds and two independent clients pass with explicit exclusions.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-006, PRD-V3N-REQ-011
**Related Decisions:** DR-V3-001

**Status:** In Progress
