### TASK-188: Eliminate peer-policy mutation starvation

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Peer-policy publication and concurrency
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Make policy mutations and concurrent classifications complete without the retained normal `peer_policy` deadline failure.

**Workflow:** Follow `../v3-release-repair-workflow.md`; resolve TASK-188 with the worktree helper, require `base_branch=v3`, root-cause investigation and verified RED/GREEN before changes, then Groundwork validation/finalization.

**Action Items:**
- [ ] Reproduce the frozen `peer_policy/concurrent_mutation_and_reads` stall. Inspect test joining/termination and `src/detail/peer_policy.cpp` shared-mutex paths; collect bounded writer-progress observations before concluding starvation, deadlock or test lifecycle error.
- [ ] Add a bounded concurrency regression which holds reader pressure until a writer completes, asserts consistent classifications and fails on missing mutation progress. Terminate/join every thread on failure; do not solve this by weakening the number of mutations/readers, adding sleeps, raising the release deadline or making the readers stop before writers need progress.
- [ ] Apply the proven repair. Prefer architecture's immutable generation publication with serialized writers if shared-mutex starvation is established; preserve atomic stance/list consistency, deny/allow precedence, typed invalid patterns, armed fast path and live mutation from handlers. Clear concurrency doubts with tests before committing.
- [ ] Check concurrent add/remove/mode/enabled publication and real public listener live policy changes; retain parser/truth-table compatibility and update architecture if the implementation publication model changes.

**Acceptance Criteria:**
- Existing `peer_policy` finishes within the unchanged 180-second release deadline under normal TLS-off/on; the bounded writer-progress regression passes under normal, ASan/UBSan and TSan.
- No data race, stale-generation use-after-free or change to documented policy truth tables; relevant native policy/accept/hook tests pass.
- C++20 and Groundwork validation pass, evidence/tracking updated, merge/cleanup completed.

**Dependencies:**
- Blocked by: TASK-187
- Blocks: TASK-189, TASK-185
**Related Requirements:** PRD-V3N-REQ-014, PRD-V3N-REQ-025, PRD-V3N-REQ-038.
**Related Decisions:** DR-V3-001, DR-V3-006, DR-V3-008; architecture §3 immutable policy generations.
**Status:** Not Started
