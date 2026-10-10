### TASK-186: Repair concurrency consumer lifetime and sanitizer-owned allocation coverage

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Concurrency tests and installed concurrency contract
**Estimate:** M
**Branch policy:** Branch from current `v3`, require `base_branch=v3`, validate and merge into `v3`, then remove the task worktree before starting TASK-187.
**Goal:** Eliminate the retained `consumer_v3_concurrency` ASan stack-use-after-scope and `task_resume_signal_lifetime` TSan link failure without suppressing lifetime or race diagnostics.

**Implementation workflow:** Resolve project/runtime context with the Groundwork helpers. Resolve worktree identity using `node /home/etr/.codex/skills/groundwork-plan-task/scripts/worktree-identity.js TASK-186`; use its returned path/branch verbatim through `groundwork-work-on`, `groundwork-debug`, `groundwork-test-driven-development`, `groundwork-validate` and `groundwork-finalize-task`. Preserve all unrelated changes, especially the separate TASK-185 worktree. Worktree creation must start from `v3`. Run a relevant baseline, record known failures as baseline, and use failing test → verified RED → minimal fix → GREEN → refactor. No release publication.

**Action Items:**
- [ ] Read the frozen ASan report at `/home/etr/progs/libhttpserver/.worktrees/TASK-185/build/task185-final-native-audit-05/asan-ubsan-off/consumer_v3_concurrency.log` and both TSan build logs referenced by that audit receipt. Reproduce only the affected targets with C++20 and the original instrumented flags. Distinguish a consumer coroutine-lambda closure lifetime error from an engine bug before choosing the fix.
- [ ] Add a failing behavioral regression that schedules the public `resume_signal` coroutine after its creation statement, observes the expected resumed/cancelled result, and executes the result assertion. The existing `main()` returns zero on both branches; make its runtime checks meaningful while retaining standalone public-header/no-library linkage coverage.
- [ ] Repair the demonstrated lifetime in `test/headers/consumer_v3_concurrency.cpp` with an owned/named coroutine function or closure lifetime that survives completion. Preserve lazy scheduling, one-shot delivery and public declarations. Change `src/httpserver/concurrency/*` only if the reproducer independently establishes a production fault.
- [ ] Make `test/unit/task_resume_signal_lifetime_test.cpp` respect the existing sanitizer-owned `operator new` convention used by `websocket_drain_race_test.cpp`. Keep normal/ASan forced-interleaving and allocation-failure coverage; under TSan execute meaningful signal/cancel/frame-retirement races that do not replace the sanitizer allocator. Record any allocation-hook-specific exclusion precisely; do not skip the owner suite wholesale.
- [ ] Add a failing compile/link or runner contract proving the TSan target can link with sanitizer allocator ownership and that normal/ASan still execute the original forced-interleaving check. Register new tests in distribution manifests when needed.
- [ ] Document cause, RED/GREEN commands, result counts and any instrumentation boundary in task evidence. Keep TASK-185's historical receipts and open acceptance unchanged.

**Acceptance Criteria:**
- The previously failing concurrency consumer runs successfully under ASan/nonrecovering UBSan for TLS-off/on; no stack-use-after-scope suppression or disabled stack checks.
- `task_resume_signal_lifetime`, resume-signal/task/cancellation owner suites and the concurrency consumer build and execute under local TSan for TLS-off/on with no duplicate allocator definitions or race report. Existing permitted TSan suppression policy is preserved and any infrastructure inability remains an explicit failure.
- Normal and ASan execute the forced timer-registration reclaim test for both signal and cancel winners; delayed public resume completes once with the asserted outcome.
- Relevant normal C++20/public-header tests pass; Groundwork validation passes, task evidence/tracking reflects the actual results, and validated work merges into `v3` with worktree cleanup.

**Dependencies:**
- Blocked by: None
- Blocks: TASK-187, TASK-185
**Related Requirements:** PRD-V3N-REQ-024, PRD-V3N-REQ-025, PRD-V3N-REQ-031, PRD-V3N-REQ-037; owned-engine sanitizer acceptance.
**Related Decisions:** DR-V3-003, DR-V3-006, DR-V3-008.
**Status:** Not Started
