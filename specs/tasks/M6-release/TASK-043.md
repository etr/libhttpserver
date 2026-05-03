### TASK-043: Doxygen / inline doc refresh

**Milestone:** M6 - Release Readiness
**Component:** Documentation
**Estimate:** M

**Goal:**
Update inline documentation on every renamed and reshaped public method so generated docs match the v2.0 surface.

**Action Items:**
- [ ] Audit every public `*.hpp`: each public method has a `///` comment block describing parameters, return value, exception spec, and (where relevant) lifetime / threading notes.
- [ ] Cross-link related methods: e.g., `block_ip` references `unblock_ip`; `register_path` references `register_prefix`.
- [ ] Document the threading contract on `webserver` class-level comment (per DR-008 distilled).
- [ ] Document error propagation on `internal_error_handler` setter and on the `webserver::run`/dispatch boundary (per DR-009).
- [ ] Document each `feature_unavailable` throw site (which method, which flag).
- [ ] Run `doxygen` and verify no warnings about missing or stale references.

**Dependencies:**
- Blocked by: TASK-031, TASK-034, TASK-041
- Blocks: TASK-044

**Acceptance Criteria:**
- `doxygen Doxyfile` runs with zero warnings.
- Spot-check 5 random renamed methods — each has a current `///` block reflecting the v2.0 signature.
- Typecheck passes.

**Related Requirements:** PRD §2 documentation NFR
**Related Decisions:** §13 documentation deliverable

**Status:** Not Started
