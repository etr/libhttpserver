### TASK-189: Repair native HTTP/1 WebSocket handshake and flood progress

**Milestone:** M14 - v3 local release readiness repairs
**Component:** HTTP/1 upgrade and WebSocket listener lifecycle
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Resolve the retained raw handshake EOF and independent `/flood` handshake timeout through the native listener.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-189, require `base_branch=v3`, investigate and write failing regressions before production fixes, validate/merge/clean through Groundwork.

**Action Items:**
- [x] Read frozen H1/WebSocket and independent supplement traces. Reproduce with `native_websocket_fixture`, `native_websocket_e2e.sh` and `run-v3-http1-websocket-gates.sh --independent`. Distinguish malformed client request, accept/readiness loss, upgrade flush, admission and response flood ordering with exact wire bytes and bounded events.
- [x] Add regression for the demonstrated handshake trigger, including a client which waits for the complete 101 before consuming flood frames. Assert Sec-WebSocket-Accept, header ordering, payload/frame integrity, and that control/handshake bytes make progress while data is backpressured.
- [x] Repair the responsible owned H1/WS/transport code or invalid client fixture according to evidence. Preserve strict RFC handshake rejection, bounded output and masking rules. Reuse `connection_engine` and WS codec/session; do not substitute a test-only server.
- [x] Run real public API managed/external listener round trips, fragmentation/control/close, excess message/output limits, client cancellation and drain deadline; verify notification once and no stranded tasks/budgets. Retain the original independent predicates and timeout bounds.

**Acceptance Criteria:**
- Raw and pinned independent WebSocket clients complete the original handshake/flood cases, exchange validated frames and close successfully within existing deadlines.
- H1/WS gate and relevant upgrade/drain/backpressure suites pass normal and ASan/UBSan TLS-off/on; no dropped handshake bytes, busy-spin or unbounded queues.
- Public installed shared/static listener witness passes; C++20/Groundwork validation and merge/cleanup complete.

**Dependencies:**
- Blocked by: TASK-188
- Blocks: TASK-190, TASK-185
**Related Requirements:** PRD-V3N-REQ-004, PRD-V3N-REQ-010, PRD-V3N-REQ-012, PRD-V3N-REQ-013, PRD-V3N-REQ-027, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-033.
**Related Decisions:** DR-V3-001, DR-V3-004, DR-V3-006, DR-V3-008.
**Status:** Complete

**Finalization evidence:** Groundwork validation passed in one iteration with eight approvals, zero fixes and zero unworked findings (run `9e1cedb5dc61bed6acc9dedc2dae66c0`). Required local receipts and all implementation build/debug artifacts are retained in the primary worktree beneath `build/task189-evidence/implementation-e889ee0e/`, with a SHA256 archive manifest (4486 records).
