# FEATURE-ci-windows-stage-make: retain the caller's staging make frontend

Status: In Progress — implementation ready; caller-owned validation pending.
Base: `v3` at `0cbf18116f63e0762aaad19b16358e6f61dddf04`.

The installed-package driver selects `MAKE=make` for configure and runtime.
The staging fixture previously searched for `gmake` first, independently of that
selection. Preserve a nonempty caller `MAKE` as one executable argument; retain
the existing `gmake`, then `make`, fallback when no executable was selected.

Action items:

- [x] Read the saved plan, literal journal, primary CLAUDE.md/AGENTS.md and v3
  architecture; create the isolated worktree from the exact base.
- [x] Corroborate the native frontend/source-path mismatch using existing
  terminal failures, one existing Windows artifact capture and official sources.
- [x] Observe the selection regression with real configure, GNU make, libtool
  and the unchanged eight-case fixture before changing the selector.
- [x] Honor the caller's selected make executable without splitting its path.
- [x] Verify the configured outer make target and all eight fixture cases with
  competing GNU make frontends; verify spaced executable/value boundaries and
  both unselected fallback choices through actual GNU make execution.
- [x] Preserve every original fixture method, DOS-prefix/MSYS handoff,
  configure-failure diagnostic and production staging/driver rule.
- [x] Run affected Python syntax and diff-whitespace checks.
- [ ] Caller: complete Groundwork validation and finalization into `v3`.
- [ ] CI/v3 PR: execute post-repair native Windows and other nonlocal checks.

Native evidence and causal assessment (2026-10-23):

- Actual package run `40030060276`, Windows job `120675819540`, and archive run
  `40030063567`, Windows job `120676108170`, report `gmake.EXE` failing to find
  the real source `Makefile.am` under `/d/a/...`. Package setup succeeds and all
  eight cases execute, producing ten assertion failures in 64.524 seconds.
- One capture of existing package artifact `12535394203` has SHA-256
  `f9a19b65963b405956087895ae5db499a7cd43a25feaa1118caa429ecad0caa5`.
  Only `matrix/summary.json` and `matrix/tls-no/build/config.log` were extracted.
  The summary binds revision `0cbf1811...` and records selected `make` as GNU
  Make 4.4.1 built for `x86_64-pc-cygwin`. The configure log records MSYS source
  paths, `/usr/bin` before inherited Windows tool directories, and Strawberry's
  native tool directory later on PATH.
- The [MSYS make package](https://packages.msys2.org/packages/make) at the exact
  installed version 4.4.1-3 supplies `/usr/bin/make.exe`, without `gmake.exe`.
  Its [build source](https://github.com/msys2/MSYS2-packages/blob/master/make/PKGBUILD)
  builds the MSYS tool. [MSYS path documentation](https://www.msys2.org/docs/filesystem-paths/)
  explains that Unix absolute paths resolve differently for native tools.
  These corroborate the frontend/path mismatch seen in the real failure.
  The runner's exact absolute `gmake.EXE` path and version were not recorded;
  Strawberry provenance is a plausible inference, not an observed identity.
- The proven defect is that the fixture discards the caller's selected frontend.
  Retaining that selection removes this mismatch without changing source-path
  policy. Native confirmation that this also resolves the Windows failure
  remains pending CI; Linux does not reproduce the Windows filesystem ABI.

Verification:

- Baseline: all eight original staging cases pass locally in 17.565 seconds.
- RED: the retained task-local `selected-make-probe.py SOURCE red` configures
  an actual outer build, then executes production `make check-v3-stage-install`.
  All eight real cases pass (18.339 seconds), but execution records show the
  selected frontend only for configure's make probes and competing `gmake` for
  all twenty fixture target calls. The selection assertion fails. Both controlled
  frontends delegate to real GNU make; no compiler, configure or pathname error
  is simulated. The initial assertion expected only `gmake`; its correction
  accounts for configure's valid selected-make calls and fails on the same
  retained execution evidence before the selector edit.
- GREEN: the same actual configure/outer-target/eight-case probe passes in
  18.984 seconds with only the selected frontend. Three smaller cases execute
  the actual fixture `make()` method against a real Makefile: an absolute
  executable path containing a space, unset `MAKE` with `gmake` available, and
  empty `MAKE` with only `make` available. The spaced executable and `VALUE=one
  two` remain intact arguments. The wrappers pass their real make executable
  into recursive recipes, keeping those shell commands outside the executable
  boundary check.
- Python compilation and `git diff --check` pass. AST comparisons confirm all
  eight test bodies and every other fixture method are unchanged. Only the
  module-level selector and its explanatory comment change. Production make
  rules, configure logic, workflows and installed-package drivers are unchanged.
  CLAUDE.md documents C++ cpplint only; no Python linter applies here.

Evidence, executable probe and task-executor notes are retained in the primary
checkout's `.debug/ci-windows-stage-make-4tk4f4a0/implementation/`; the existing
literal journal records the bounded assessment. No new native CI, broad suite,
publication, merge or full validation was performed. Previous unrelated failure
assessments remain closed with their existing uncertainty. Missing post-repair
native evidence remains assigned to CI/v3 PR under AGENTS.md.
