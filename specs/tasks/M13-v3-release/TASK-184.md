### TASK-184: Remove MHD build/link paths and bump v3 SOVERSION

**Milestone:** M13 - v3.0 release
**Component:** Build and packaging
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Remove MHD build/link paths and bump v3 SOVERSION for libhttpserver v3.0.

**Action Items:**
- [ ] Remove all MHD configure, build, source and package paths.
- [ ] Bump SOVERSION and validate parallel installation.
- [ ] Audit clean artifacts for MHD headers, symbols, flags and probes.

**Dependencies:**
- Blocked by: TASK-179, TASK-182, TASK-183
- Blocks: TASK-185

**Acceptance Criteria:**
- Clean TLS-on/off artifacts contain no MHD headers, symbols, link flags or probes; installed v3 ABI is distinct.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-003, PRD-V3N-REQ-037
**Related Decisions:** DR-V3-001

**Status:** Not Started
