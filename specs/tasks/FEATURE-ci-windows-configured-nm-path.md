# FEATURE-ci-windows-configured-nm-path

Status: Complete — local validation passed; native Windows proof remains CI-owned/incomplete.
Base: v3 `a9c800522afe6d2abea308e0be9215fda1b2826e`
Plan: `.groundwork-plans/FEATURE-ci-windows-configured-nm-path-plan.md`
Journal: primary `.debug/ci-a9c-windows-nm-path/journal.md`

Repair the native Windows handoff of the exact configured symbol inspector.
The retained package receipt selects `NM = /mingw64/bin/nm -B`; native Python
passes that MSYS executable spelling directly to CreateProcess and fails with
WinError2 before an inspector starts. The bounded assessment supports this path
handoff defect; it establishes no internal inspector or library-symbol result.

- [x] Reproduce the raw configured path at a controlled native process boundary
      before implementing the repair.
- [x] On Windows, translate only an MSYS absolute executable with the selected
      PATH's `cygpath -m -- <literal-path>`, invoked directly without a shell.
- [x] Preserve quoted paths, selected command options, option operands, archive
      identity and demangling. Leave native drive/UNC, bare command and POSIX
      selections, including FreeBSD's configured inspector, unchanged.
- [x] Keep conversion failure, unavailable conversion tool, empty conversion,
      unavailable selected executable and nonzero inspector results fatal.
      Never fall back to another inspector.
- [x] Preserve all 24 required uppercase-T definitions and existing strict
      missing/undefined/lowercase/empty/malformed selection checks.
- [x] Run focused checker and existing small installed-lane fixtures; retain
      literal RED/GREEN logs and syntax/diff evidence in primary journal's
      `implementation/` directory.
- [x] Caller-owned Groundwork validation sealed PASS: session `e4d1040c1fb60731690dec2acf4cc313`, all9 independent reviewers approve,1 iteration,0 fixed,0 unworked. Local finalization/merge into v3; native Windows post-fix proof pending.

## Implementation verification

The new raw-path regression fails before the repair because the boundary receives
`/mingw64/bin/nm`. After the repair, 8 new boundary tests and 6 existing checker
tests pass. They execute real local converter/inspector fixture subprocesses,
substituting only native process lookup and the checker's own platform binding.
These controlled fixtures establish literal argv and failure handling, not
native Windows execution.

Three existing `ConfiguredSymbolEntryPointsTest` installed-lane cases pass:
generated selection wins over ambient NM; missing and empty generated selection
fail. Each uses the existing tiny Autotools project with real local compiler,
install and child-process handoff, without building the library. The owning Make
recipe and installed-lane handoff remain unchanged.

Changed Python modules compile and `git diff --check` passes; no standalone
Python lint command is documented. No C++ source, runtime auditor, workflow or
required symbol contract changes. No full baseline or consumer suite was run.

The two earlier `InstalledMakeTest` configure-clock failures remain
FAILED/INCOMPLETE and CLOSEDUNKNOWN. They were not retried, repaired or waived.
The preceding kqueue diagnostic is closed after 25 comparisons/first mismatch;
its retained diagnostic branch is never merged or cherry-picked. Native Windows
post-fix proof remains INCOMPLETE and CI/v3 PR owned. Executor does not validate,
merge, push, dispatch CI or clean up worktrees.
