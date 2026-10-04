### TASK-123: Integrate WebSocket close with cancellation and server drain

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** WebSocket codec and adapters
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide WebSocket close with cancellation and server drain for libhttpserver v3.0.

**Action Items:**
- [x] Coordinate WebSocket Close with server drain deadline.
- [x] Wake blocked send/receive on cancellation.
- [x] Report one reason without affecting unrelated connections.

**Dependencies:**
- Blocked by: TASK-110, TASK-121, TASK-122
- Blocks: TASK-128

**Acceptance Criteria:**
- Drain sends Close, waits to deadline, then cancels once with a recorded reason.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-013, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-033
**Related Decisions:** DR-V3-008

**Status:** Complete

**Checkpoint (2026-10-03):** Drain/Close/cancellation implementation and tests
are preserved on `task/TASK-123`. Final validation is blocked by an inherited
HTTP exchange suspension flag race observed by current TSan after the private
protocol-transfer timer guard. Prior 218/218 runtime and static/Doxygen passes
predate that guard. Action items remain unchecked pending final validation;
see [implementation evidence](../../architecture/v3/TASK-123-design-evidence.md).

**Local resume (2026-10-04):** The suspension observation race and a second
disconnect-state publication race have deterministic RED/GREEN regressions and
final fresh ASan/UBSan/TSan proof across 13 native suites plus actual drain clients.
Public default move semantics are preserved. The standalone executor include
defect has a direct standard include and registered consumer. Implementation is
ready for independent full validation; status and action items remain unchanged
until the caller completes that phase. Historical 218/218 results are not final
acceptance for this repair.

**Final validation (2026-10-04):** Validation passed after the released-connection
submission/rearm and completed-empty-response repairs. Fresh normal validation
passed 219/219 registered tests with no failures, skips, or errors. ASan/UBSan
passed 195 tests and 5,736 checks; TSan passed 195 tests and 5,741 checks. Raw and
independent WebSocket clients passed, and every owned validation process exited.
Five affected-domain closure reviews approved the final repair; existing
security, architecture, and housekeeping approvals carried forward. The official
complexity and duplication checks retained unchanged inherited baseline findings.
Seven nonblocking issues are recorded in the
[unworked review report](../../unworked_review_issues/task-123_validation_bd69982caea90fff759db82cd290743e.md).
Durable local receipts remain under `.git/groundwork/validation/cdb4ee2aea69cc6a/2cd710a0e04dfe7a/groundwork-validation-bd69982caea90fff759db82cd290743e`.
