### TASK-088: Re-enable helgrind / drd / sgcheck in valgrind CI lane

**Milestone:** M7 - v2 Cleanup
**Component:** `.github/workflows/verify-build.yml`
**Estimate:** M

**Goal:**
`verify-build.yml:766` configures the Valgrind lane with
`--disable-valgrind-helgrind --disable-valgrind-drd --disable-valgrind-sgcheck`.
Only memcheck runs; the race detectors (helgrind, drd) and stack-overrun
checks (sgcheck) never execute.

**Action Items:**
- [ ] Remove the three `--disable-valgrind-*` flags from the configure invocation in the Valgrind lane.
- [ ] Run the suite locally under helgrind: triage every finding. Fixes go in the source; intentional racing patterns get a narrow entry in a new `test/valgrind-helgrind.supp` with a comment.
- [ ] Repeat for drd. Note that helgrind and drd produce different shaped warnings on identical code; both must be clean.
- [ ] Repeat for sgcheck if the upstream Valgrind version in CI still ships it. (Valgrind 3.20+ removed sgcheck — if so, document the removal and skip the sgcheck step.)
- [ ] Wire the suppression file(s) into the lane.
- [ ] Update the audit-flagged comment block.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- Valgrind lane runs memcheck + helgrind + drd cleanly on every PR.
- sgcheck either runs cleanly or is documented as removed upstream.
- Any race suppression in the new files is narrow (no wildcards over libhttpserver symbols).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 security NFR, PRD §3.6 sanitizer-clean
**Related Decisions:** DR-008 (thread-safety contract)

**Status:** Backlog
