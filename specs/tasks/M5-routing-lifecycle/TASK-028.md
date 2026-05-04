### TASK-028: Routing-semantics regression gate

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Route table
**Estimate:** M

**Goal:**
Run v1's full routing-test corpus against the new 3-tier table; treat any regression as a release-blocker.

**Action Items:**
- [ ] Inventory v1's existing routing tests (likely under `test/`); list every distinct routing pattern they cover (exact, parameterized with one segment, parameterized with multiple, prefix, regex, method-mismatched).
- [ ] If any test was tightly coupled to v1's three-map internals, port it to the new public API; otherwise expect it to pass unchanged.
- [ ] Run the full corpus against the new implementation and triage any failures: spec deviation (file ticket / fix architecture) vs. implementation bug (fix it).
- [ ] Document the corpus as the v2.0 routing regression gate in `test/README` (or equivalent).

**Dependencies:**
- Blocked by: TASK-027
- Blocks: None (release-quality gate)

**Acceptance Criteria:**
- 100% of v1 routing tests pass against the v2.0 implementation.
- Any divergence from v1 routing semantics is documented (with rationale) or fixed.
- The corpus is wired into `make check` so future commits can't regress it.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-002, PRD-HDL-REQ-004
**Related Decisions:** AR-003 (release-blocker risk), §9 testing item 5

**Status:** Not Started
