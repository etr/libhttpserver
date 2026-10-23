# FEATURE-ci-freebsd-selected-make: propagate the selected make into package configure

Status: Complete (local) — validated and finalized into v3; native CI remains pending.
Base: local `v3` at `c2d5c626f456f218676a6d840c8ed1442aa28438`.

FreeBSD package job `120468733763` and archive job `120469466638` passed
`--make gmake` to the installed-consumer driver, then failed real
`config.status` dependency-fragment bootstrapping with the suggestion to use
`MAKE="gmake"`. Their effective configure environment was not logged. Source
inspection established that the driver selected `args.make` for build/install
commands but omitted it from configure's environment.

The package lane now sets `MAKE` to exactly `args.make` for configure, build,
install and the merged check environment. The runtime environment also keeps
that selection, so an inherited `MAKE` cannot replace it during the merge.
The CLI default remains `make`; named GNU make and absolute selections are
preserved. Dependency tracking, compiler/provider flags, TLS selection,
Windows shell/path/prefix handling and process ownership remain unchanged.

Action items:

- [x] Create the isolated Groundwork worktree from the exact local v3 base; preserve other work.
- [x] Read the existing literal failure journal and actual FreeBSD error before diagnosis.
- [x] Establish RED through the production `package_lane` and a real Autoconf/Automake C project.
- [x] Propagate the selected make into configure and subsequent lane environments.
- [x] Verify named `gmake` with an unusable default `make`, absolute make with conflicting ambient `MAKE`, and default `make` with conflicting ambient `MAKE`.
- [x] Verify actual generated dependency fragments and compile/header dependencies, plus make selection inside build/install/check recipes.
- [x] Keep the regression in the existing installed-consumer test script and fixture/distribution registration.
- [x] Run the affected Python preservation tests, syntax and diff checks; retain raw configure evidence and source bindings.
- [x] Caller: complete Groundwork validation and local finalization into `v3`.
- [ ] CI / v3 PR: fresh native FreeBSD package/archive execution; other nonlocal execution remains CI owned.

Acceptance evidence: the original driver and the repaired driver execute the
same final regression fixture. All three original-driver cases fail in real
`config.status` with the automatic dependency-tracking error. All three fixed
cases successfully configure, compile and install, then reach the fixture's
deliberately failing check target. The tests require the check marker,
`.deps/probe.Po` containing `probe.h`, and exact `MAKE` values in all three
recipe receipts. This bounded fixture stops before package consumer/runtime
work and is not a package matrix or native FreeBSD receipt.

The three new integration tests and 22 existing Windows-shell, optimization
and matrix contract tests pass. Python compilation and `git diff --check`
pass. No Python linter is configured in repository guidance. The fixture
uses local Autoconf 2.71, Automake 1.16.5, GNU Make 4.3, GCC 13.3.0 and
Python 3.12.3. POSIX make-lookup integration is skipped on Windows; existing
Windows shell contracts continue to execute. No full baseline suite or
unrelated investigation was run. Closed Darwin/Linux/Windows concurrency
uncertainty remains closed; no new incidental failure occurred.

Primary journal, exact commands, raw RED/GREEN output, generated configure,
config.log/config.status, command receipts, dependency/recipe artifacts and
source SHA-256 bindings are retained in the main repository's ignored
`.debug/ci-freebsd-selected-make-zgo8kkt1/`. `baseline-actual-configure/` and
`fixed-actual-configure/` retain the same final test source binding and the
original/repaired executed drivers. Native FreeBSD proof remains incomplete
and is CI / v3 PR owned under AGENTS.md; it does not block local completion.

Project notes / task-executor memory: v3 is Autotools/C++20; this Python-only
repair needs no full library build/provider setup. Focused checks use
`python3 scripts/test-v3-installed-consumer.py InstalledMakeTest InstalledShellTest InstalledShellPSTest MatrixTest`
and `python3 -m py_compile` for changed scripts. The real fixture is generated
with `autoreconf --install --force` and invokes the production lane and
Commands runner without mocking configure or make. Git metadata mutations
need the existing sandbox escalation; Groundwork's identity helper resolves
the task worktree/branch. The installed fixture target and EXTRA_DIST already
include this test script, so no build-system registration change is needed.

Groundwork validation passed at `2e5beffbb8acf227356ea4e9528a949e4b1887ae`
(1 iteration, 0 fixed, 0 unworked; all eight applicable reviewers approved).
Native FreeBSD and other nonlocal checks remain incomplete and CI/v3-PR owned.
