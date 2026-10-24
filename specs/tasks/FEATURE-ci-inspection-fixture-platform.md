# FEATURE-ci-inspection-fixture-platform

Status: In Progress (implementation verified; caller review/finalization pending)
Base: local v3 `7880e6f01b6f42ee609d45403b858d8c62893ba9`

Repair the inspection-failure fixture's shared stdlib platform mutation, which
causes the real Darwin compiler to receive GNU linker-map flags before the
intended ELF inspection error. Replace only the audit module's platform binding.
Keep real native compilation, forced FreeBSD inspection and all existing raw
output, exit status, failed artifact and receipt-persistence assertions.

- [x] Read the closed native failure/source assessment and verify the affected local baseline.
- [x] Add a failing regression observing the actual compile host while delegating real compilation.
- [x] Isolate the audit platform binding with native attributes preserved.
- [x] Verify the affected class, full installed-consumer script, receipt recovery, Python syntax and diff.
- [ ] Caller review and finalization.

Evidence: primary `.debug/ci-7880-darwin-linkmap-fixture-6vfr9ed2/journal.md`
and `implementation/` logs beside that journal. Native macOS package job
`120773457663` and archive Darwin job `120773609587` failed with three errors
in 64 tests (three existing skips). The bounded causal assessment is closed.
Local original inspection class: three tests passed; this does not establish
Darwin linker acceptance or undo the failed native gates.

Native post-fix Darwin checks remain incomplete and CI/v3 PR owned; they do not
block local completion under AGENTS.md. Existing FreeBSD runs are preserved.
Header/outbox and FreeBSD-format uncertainties remain closed and uninvestigated.
No production compiler, linker-map, audit policy or recovery workflow changes.

TDD: the new test failed locally with actual compile/inspection hosts
`('FreeBSD', 'FreeBSD')` instead of captured native `('Linux', 'FreeBSD')`.
After binding isolation, the affected class passed all four tests; the complete
installed-consumer script passed all 65 tests with no skips. The real compiler
produced the consumer executable and link map, then the deliberately unrecognized
inspector output still raised the expected original ELF inspection failure.
The package/archive early inspection receipt recovery regression passed (one
test). Python byte-compilation and `git diff --check` passed. No standalone
Python linter is documented; the repository cpplint rule applies to unchanged C++.
No broad baseline suite, project rebuild, native Darwin probe or CI control ran.
