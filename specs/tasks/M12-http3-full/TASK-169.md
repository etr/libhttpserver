### TASK-169: Integrate dynamic QPACK with HTTP/3 request scheduling

**Milestone:** M12 - Full HTTP/3
**Component:** HTTP/3 connection engine
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide dynamic QPACK with HTTP/3 request scheduling for libhttpserver v3.0.

**Action Items:**
- [x] Negotiate dynamic capacities and connect QPACK to H3 streams.
- [x] Resume blocked headers without starving critical streams.
- [x] Release references on reset, cancellation and drain.

**Dependencies:**
- Blocked by: TASK-161, TASK-168
- Blocks: TASK-170, TASK-171, TASK-174, TASK-177

**Acceptance Criteria:**
- Concurrent dynamic headers, cancellation and encoder reordering interoperate without control-stream starvation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-008, PRD-V3N-REQ-017, PRD-V3N-REQ-027
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** Complete

Local implementation and focused verification are complete; runner/coordinator
validation remains pending. Evidence: `docs/task-169-http3-qpack-scheduling.md`.
Nonlocal platform and native-provider network checks remain CI/v3 PR work.
