# FEATURE-ci-configured-symbol-tool

Status: In Progress
Base: v3 `4003ed97e112fefc5fa68b67e4e5868daae1bad2`
Plan: `.groundwork-plans/FEATURE-ci-configured-symbol-tool-plan.md`
Journal: primary `.debug/ci-6b2-freebsd-nm-assessment-7tg4peed/journal.md`

Honor the build's actual configured NM at the compiled-operation Make target and
installed consumer checkpoint. The retained FreeBSD failure invoked bare `nm -C`
despite generated `NM = /usr/local/bin/nm -B`. Its exact crashed tool version and
internal crash cause are unknown; this change repairs the configured-tool handoff.

- [x] Support explicit selected symbol-tool command/arguments with safe argv execution.
- [x] Pass actual configured NM consistently at both real entrypoints.
- [x] Exercise checker and generated Make/lane entrypoints with a conflicting
      default tool, options, quoted spaces, and installed archive identity.
- [x] Preserve all 24 required names, uppercase T definitions, demangling and
      fatal missing/undefined/nonzero/empty/malformed-tool behavior without fallback.
- [x] Complete focused checks, Python compilation and build-definition validation.
- [ ] Caller-owned validation/finalization.

Native FreeBSD post-fix checks remain INCOMPLETE and CI/v3 PR owned. No full native
build or baseline suite, SDK installation, CI retry, merge or publication is part
of this implementation. Other closed investigations remain closed.

Pre-edit focused baseline: 9/11 tests pass; two unchanged InstalledMakeTest cases
fail in configure sanity with `newly created file is older than distributed files!`.
Those results remain failed/incomplete; the exact timestamp cause is unproven.

## Implementation verification

- 10 new checker/generated-Make/lane regressions PASS, with meaningful pre-edit
  RED for missing CLI and both entrypoints selecting the default PATH inspector.
- 31 existing shell/inspection/matrix consumer fixtures PASS; 4 package workflow
  checks PASS. The earlier two InstalledMakeTest sanity failures remain failed
  and incomplete; they were not retried. Other unrelated broad tests were not run.
- Bootstrap and Automake generation PASS (existing .PHONY/AC_OUTPUT warnings);
  all three changed Python modules compile; `git diff --check` PASS.
- One offline explicit `--nm=/usr/bin/x86_64-linux-gnu-nm -B` check passes on the
  exact retained 122420834-byte FreeBSD archive, SHA-256
  `3d1f70de66312ad5fbefaeb706a5b32d650a56945d362850c963d04d30360852`.
  This uses local GNU Binutils 2.42, executes no foreign program, and confirms the
  unchanged 24-operation contract. Native configured GNU 2.44 is unverified.

Literal RED/GREEN/build-generation logs, source-bound static checks and archive
receipt are durable in the primary journal's `implementation/` directory. No
production C++, installed headers, ABI, runtime/dependency policy, required symbol
names, uppercase T predicate or demangling requirement changed. Selected tool
failure never triggers another inspector. Local implementation is ready for
caller-owned validation; status stays In Progress through that handoff. Native
post-fix FreeBSD/other-platform evidence remains INCOMPLETE and CI/v3 PR owned.
No push, merge, cleanup or CI controls were performed by this executor.
