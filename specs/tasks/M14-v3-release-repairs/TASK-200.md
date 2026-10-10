### TASK-200: Repair intermittent timer and executor teardown race

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Concurrency timer ownership and executor teardown
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Resolve the observed `task_race` TSan teardown failure before the final packaged local inventory.

**Workflow:** Follow `../v3-release-repair-workflow.md`; use the Groundwork identity helper, branch from `v3`, systematic debugging, verified RED/GREEN, validation and finalization. Preserve historical receipts and unrelated worktrees.

**Action Items:**
- [ ] Read TASK-187's unresolved extra finding and immutable receipts at `build/task187-evidence/implementation-ad76a561/build/task187-implementation/tsan-off/extra-task-race-failure.log`, `focused-check.log`, and `base-task-race-identities.json`. The observed failure is `pthread_cond_destroy` versus timer-thread `manual_executor::post`, exit 66; a single equivalent unchanged base comparison passed. Treat this as unresolved intermittent behavior, not a proven production or fixture cause.
- [ ] Identify the timer, callback, task frame and executor lifetime boundary using bounded deterministic interleaving. Add a regression that fails for the demonstrated cause and cleans up all threads/owners even on failure. Do not replace a failing diagnostic with retries, sleeps, longer deadlines or a new suppression.
- [ ] Repair the proven production or fixture ownership boundary while preserving executor borrowing, cancellation, one-shot delivery and timer cleanup contracts. Use the smallest causal change; inspect public/API implications before changing a contract.
- [ ] Execute affected task/timer/resume/cancellation suites under normal, ASan/nonrecovering UBSan and TSan in TLS-off/on, with leak/stack checks and existing suppressions unchanged. Run relevant installed consumers and record assertion counts, RED/GREEN receipts and ownership conclusions.

**Acceptance Criteria:**
- A meaningful deterministic regression demonstrates and closes the observed race with exactly-once outcomes and safe executor/timer teardown.
- Required affected local sanitizer lanes and public consumers pass; no required check is waived and no race suppression is added.
- Groundwork validation passes, evidence and tracking are updated, and the task is merged/cleaned before TASK-185 resumes.

**Dependencies:**
- Blocked by: TASK-199
- Blocks: TASK-185
**Related Requirements:** PRD-V3N-REQ-024, PRD-V3N-REQ-025, PRD-V3N-REQ-031, PRD-V3N-REQ-037; owned-engine sanitizer acceptance.
**Related Decisions:** DR-V3-003, DR-V3-006, DR-V3-008.
**Status:** Not Started
