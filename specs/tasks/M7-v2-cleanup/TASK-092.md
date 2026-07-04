### TASK-092: Wire `route_table_concurrency` TSan + `stop()`-from-handler deadlock test into per-PR CI

**Milestone:** M7 - v2 Cleanup
**Component:** `test/Makefile.am`, `.github/workflows/verify-build.yml`
**Estimate:** S

**Goal:**
Two stress/contract tests live in the tree but are never run on per-PR CI:
- `test/Makefile.am:252-253` — `route_table_concurrency` TSan run is manual-only.
- `test/Makefile.am:264-267` — `stop()`-from-handler deadlock test gated behind `HTTPSERVER_RUN_STOP_FROM_HANDLER=1`, skipped by default in CI.

Both have history of catching real regressions; wire them into the matrix.

**Action Items:**
- [ ] In the TSan CI lane of `verify-build.yml`, append a step that runs `make check-route-table-concurrency` (or whatever the existing manual target is named). Time-box to ≤ 2 minutes so it doesn't bloat the matrix run; pick an iteration count that hits the typical regression window.
- [ ] Add a separate CI step (Linux gcc lane, not sanitizer-gated) that sets `HTTPSERVER_RUN_STOP_FROM_HANDLER=1` and runs the stop-from-handler deadlock test once. Failure mode is a hang → use the CI step's timeout to catch it.
- [ ] Remove the "manual-only" comment in `test/Makefile.am` once both are wired.
- [ ] Document both in `test/PERFORMANCE.md` (or wherever stress tests are catalogued) so future test authors know the contract.

**Dependencies:**
- Blocked by: TASK-032 (Done; stress test infra)
- Blocks: None

**Acceptance Criteria:**
- Per-PR CI runs `route_table_concurrency` under TSan.
- Per-PR CI runs the stop-from-handler deadlock test with the env gate set.
- `test/Makefile.am` no longer marks either as manual-only.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 test reliability NFR, DR-008 (thread-safety contract)
**Related Decisions:** DR-008

**Status:** Done
