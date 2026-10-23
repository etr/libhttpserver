# FEATURE-ci-freebsd-pid-zero: accept the FreeBSD kernel process snapshot

Status: Implemented; caller validation and finalization pending.
Base: `v3` at `48e4d0208dd0d319e388571481263fb009878d28`.

FreeBSD package job `120396472458` in run `39948363870`, source
`a68ba478e967988331c167af102189c905b6fe95`, fails during successful command
cleanup with `invalid command process snapshot PID`, before the library
matrix runs. That revision and the task base share the exact command module
blob `e75901dededdf996ef70a4bfa723024825a38122`. The job did not log the
rejected row, so attribution to kernel PID0 is corroborated rather than
verified from a raw native row. The same a68 FreeBSD archive job
`120398555704` / run `39948390044` has the same cleanup rejection after
Node build completion; its raw log is retained alongside the package log.
Rejecting legitimate PID0 is independently
reproduced through the production parser.

The [FreeBSD Architecture Handbook](https://docs.freebsd.org/en/books/arch-handbook/boot/)
identifies the scheduler/swapper as PID0. The
[FreeBSD kernel initialization source](https://github.com/freebsd/freebsd-src/blob/releng/14.3/sys/kern/init_main.c)
places proc0 in pgrp0. The parser now retains numeric PID0 with parent0/group0
as observation data. Duplicates, malformed or negative fields, invalid states,
and nonzero parent/group for PID0 still fail closed. Signal targets continue
to come exclusively from the owned, unreaped, positive-PID session leader.

Action items:

- [x] Use the Groundwork identity helper and isolated worktree from the exact
  v3 base; preserve unrelated bauth work and closed investigations.
- [x] Read process safety/TDD guidance and record concrete doubts before code.
- [x] Reproduce rejection through actual `_process_snapshot` with kernel0,
  an owned leader, and a live descendant before editing production.
- [x] Accept only the legitimate zero-parent/group kernel row; retain strict
  duplicate, malformed, negative, state and empty-snapshot controls.
- [x] Exercise real command/descendant/socket cleanup and real ps with a
  kernel row, requiring positive signal targets and preserved output receipts.
- [x] Preserve leader reservation/reaping, permission errors, exact group
  targeting, two-observation quiescence and shared cleanup deadlines.
- [x] Run the affected installed-consumer module, Python syntax and diff checks.
- [x] Save raw RED/GREEN evidence, source binding and journal in main
  `.debug/FEATURE-ci-freebsd-pid-zero/`.
- [ ] Caller: Groundwork validation and local finalization into v3.
- [ ] CI / v3 PR: native FreeBSD package execution and nonlocal checks.

Verification: three existing ps validation/ownership checks passed before
edits. The new parser and real cleanup tests rejected the valid kernel row
with the original exception in RED; after the minimal guard correction all
three new tests pass, including 11 invalid/empty snapshot cases. All 58 tests
in the affected installed-consumer module pass. Existing permission denial,
ownership loss, unknown states, late descendants, deadlines and shell controls
remain covered. Python AST syntax and `git diff --check` pass. CLAUDE.md's C++
linter does not apply to these Python changes; it documents no Python linter.

These checks use controlled ps data or the local real ps with an added
kernel row, and real local processes/sockets; they are not native FreeBSD
receipts. Native checks remain incomplete and CI/v3-PR owned under AGENTS.
No full native baseline, installation, workflow/runtime changes, validation
loop, merge or publication was performed. Linux parity and Darwin deadline
causality remain CLOSED/UNCERTAIN; further Darwin diagnostics still require
the user's explicit approval.

Project notes: Python standard library tests need no installation. Full
Autotools/C++20 builds are outside this script-only repair. Groundwork
returned the exact task path/branch; Git metadata writes need escalation.
