# FEATURE-ci-darwin-test-contracts

Status: In Progress (implementation ready; caller validation/finalization pending)
Base: local v3 `a5a72356741300e5da16b94090f0f26a67a2d1f7`

Repair four native-reproduced test/setup assumptions without changing production
runtime, quota accounting, APIs, peer addresses or unrelated workflows.

- [x] Test returned continuation behavior with an explicitly owned coroutine frame.
- [x] Inject all five actual QPACK precommit allocations and prove blocked request nonpublication, with a successful control.
- [x] Size the shared response fixture budget from actual reserved string capacity.
- [x] Prepare and clean up only the Darwin selected-archive proof's owned loopback alias; execute lifecycle regressions.
- [x] Run focused local checks and record native verification limits.
- [ ] Caller validation and finalization.

Evidence: primary `.debug/ci-darwin-test-contracts-8m2spsq4/journal.md`.
Native RED batch 40055653509 is closed. Diagnostic refs f6475daf/88cd82fd
must never merge or cherry-pick into v3. Header/outbox incidents remain closed
with uncertain cause; this task does not retry or repair them. Native post-fix
Darwin, Windows and BSD checks remain incomplete and CI/v3 PR owned.

Local verification: each of the three focused C++ targets passes with GNU and
actual libc++20 on Linux (47 cases / 2901 checks per standard library, no skips).
The lifetime target and concurrency implementation pass Clang ASan/UBSan with
leak and stack checking enabled (3 cases / 1816 checks), using supported execution
outside sandbox ptrace. Workflow tests pass (28 tests, including 6 lifecycle tests
covering 13 absent/preexisting/failure/signal scenarios). Changed-file cpplint,
actionlint, workflow Bash syntax, Python compilation and diff whitespace pass.

TDD uses the retained native RED for the three platform-specific fixture failures;
new workflow lifecycle regressions failed before the setup change and pass after it.
Original GCC sized-delete sanitizer setup and sandbox LSan failures are preserved
in the journal; neither is reported as a passing run. Two Automake wrappers were
interrupted during unrelated prerequisite compilation before tests; verification
then executed only the three requested binaries. No production owner expansion,
full baseline suite, native rerun, CI control or publication occurred.
