### TASK-178: Gate full HTTP/3 and WebSocket-over-H3 conformance

**Milestone:** M12 - Full HTTP/3
**Component:** Build/packaging/validation
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide full HTTP/3 and WebSocket-over-H3 conformance for libhttpserver v3.0.

**Action Items:**
- [x] Aggregate RFC 9114/9204/9220 transcripts and independent clients.
- [x] Gate QUIC/H3/WS malformed-input and slow-peer tests.
- [x] Publish exact tool versions, exclusions and artifacts.

**Dependencies:**
- Blocked by: TASK-173, TASK-174, TASK-175, TASK-176, TASK-177
- Blocks: TASK-179, TASK-180, TASK-181, TASK-182

**Acceptance Criteria:**
- All applicable RFC 9114/9204/9220, interop, fuzz and bounded-memory gates pass before release.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-011, PRD-V3N-REQ-012, PRD-V3N-REQ-013
**Related Decisions:** DR-V3-001

**Status:** In Progress

**Implementation evidence (2026-10-09):** The repaired aggregate at
`build/task178-repair-full-02/manifest.json` passed all nine required local
stages: 206 native/sanitizer suites and 2,258 cases, all twelve QUIC matrix
cells with closure witnesses, both independent HTTP/3 clients, RFC 9220,
and seven instrumented fuzz targets with 2,000 runs each. The task-local QUIC
driver explicitly selects ns-3 real-time mode at process initialization;
the setting regression and runner contracts pass. Earlier failures are retained
as historical evidence. Changes remain unstaged for the runner-owned commit
flow; task status remains In Progress pending the normal runner lifecycle.
Nonlocal platform checks remain deferred to CI/the v3 PR per repository policy.
See [observed evidence](../../../docs/task-178-http3-full-conformance.md).
