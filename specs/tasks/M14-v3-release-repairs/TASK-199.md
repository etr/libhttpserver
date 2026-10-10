### TASK-199: Deliver packaged public-capability and benchmark adapters for release execution

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Installed package verification and release receipt adapters
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Deliver executable installed-public/benchmark adapters and tested inputs ready for TASK-185's final runner wiring, and reconcile current capability documentation.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-199, require `base_branch=v3`, RED/GREEN receipt contracts, Groundwork validation/finalization. This task owns only its `v3` worktree; do not edit the preserved TASK-185 worktree or prematurely merge its runner implementation.

**Action Items:**
- [ ] Consolidate TASK-193..198 public/benchmark runners into bounded package-callable entry points with explicit archive/source/install/tool identities, TLS/linkage/loop configurations, scenario inventory and immutable receipt output. Reuse installed-package modules; avoid a general framework.
- [ ] Add failing contract tests which reject source/private-header consumers, a different installed library, missing credential/profile/protocol/cancellation/drain/budget/WS scenarios, absent clients, nonzero/timeout clients, stale artifact identities, malformed benchmark samples and exit-zero without meaningful witnesses.
- [ ] Execute a fresh focused packaged adapter integration using installed shared/static managed/external clients; validate receipts independently and prove tampering fails. Ensure TLS-off/on declaration/dependency audits and new installed configuration expectations reflect genuine support.
- [ ] Provide documented call signatures/receipt schema and exact contract commands for185 to replace its two unconditional blocked lambdas. TASK-185 owns that wiring after merging current `v3`; do not alter or weaken its inventory/check predicates in this dependency task.
- [ ] Reconcile current README/native/TLS/migration docs, capability reports, architecture implementation notes and task index with executed public evidence. Correct TASK-182 stale index status from actual evidence. Preserve historical release documents/receipts and leave185 acceptance open. No unrelated baseline backend-size cleanup.

**Acceptance Criteria:**
- Both new capability families have runnable archive/install-aware entry points, deterministic negative contracts, complete scenario receipts and successful focused installed integration evidence.
- No private-engine success, unbound listener, empty benchmark, stale/mismatched artifact or incomplete scenario set can satisfy the adapter contract.
- C++20 installed/package checks and Groundwork validation pass; current documentation/tracking agrees, task merges/cleans, and documented adapters are available for185's final wiring.

**Dependencies:**
- Blocked by: TASK-198
- Blocks: TASK-185
**Related Requirements:** PRD-V3N-REQ-001 through PRD-V3N-REQ-038 as applicable to the complete public-capability inventory; TASK-185 native-throughput-latency inventory.
**Related Decisions:** DR-V3-001 through DR-V3-008; architecture §5 packaging and validation.
**Status:** Not Started
