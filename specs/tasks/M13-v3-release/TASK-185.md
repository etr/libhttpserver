### TASK-185: Run final dependency, conformance, sanitizer and performance release gates

**Milestone:** M13 - v3.0 release
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide final dependency, conformance, sanitizer and performance release gates for libhttpserver v3.0.

**Action Items:**
- [ ] Run all protocol, TLS, dependency, sanitizer and performance release suites on packaged artifacts.
- [ ] Record release tool versions, exclusions and exact pass/fail receipts.
- [ ] Require every v3.0 in-scope gate to pass before tagging.

**Dependencies:**
- Blocked by: TASK-180, TASK-181, TASK-182, TASK-183, TASK-184
- Blocks: None

**Acceptance Criteria:**
- All v3.0 acceptance gates pass on the release artifact with tool versions and failures retained.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-004, PRD-V3N-REQ-005, PRD-V3N-REQ-006, PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Not Started
