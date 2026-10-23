# FEATURE-ci-windows-stage-fixture: execute the real staging fixture through MSYS

Status: Complete (local) — validated and finalized into v3; native Windows confirmation pending.
Base: local `v3` at `337f6161be9897e590fa5155658582f3dca7cd34`.

Native Windows job `120582231736` fails the new staged-install fixture during
configure setup, before any tests run. Bootstrap installed the auxiliary files;
the fixture handed an unqualified shell a native drive-bearing source path.
Autoconf splits auxiliary-file search directories on `:`, so configure needs an
MSYS source path. The configured `D:/ci/prefix` remains the staging test subject.

Action items:

- [x] Read primary checkout CLAUDE.md/AGENTS.md, saved plan, literal journal,
  retained native failure log, complete fixture and existing production adapter.
- [x] Create the Groundwork task worktree from exact local v3, preserving other work.
- [x] Use the absolute PATH-selected bash on Windows for configure, path conversion
  and both real libtool invocations; fail if the selected PATH has no bash.
- [x] Convert source-script and installer/ranlib executable handoffs with actual
  `cygpath -u` in the selected shell. Keep `--prefix=D:/ci/prefix` unchanged.
- [x] Preserve all eight real configured Makefile/libtool/installed-header cases
  and all original negative assertions without skips or mock configure.
- [x] Run the real eight-case local regression and source-bound executed boundary
  proof; check syntax, unchanged test bodies/production sources and diff whitespace.
- [x] Caller: full required Groundwork validation and finalization into v3.
- [x] Caller: composed conventions review of `c0cfdabc..final-task-HEAD` against
  primary checkout CLAUDE.md; carry other cleared profiles unless disturbed.
- [ ] CI / v3 PR: fresh native Windows checkout/archive installed matrices.

Local evidence:

- Native RED: retained raw job log shows `cannot find required auxiliary files:
  compile ltmain.sh config.guess config.sub missing install-sh`, `Ran 0 tests`.
- `./bootstrap` passes with existing .PHONY / AC_OUTPUT warnings.
- `python3 scripts/test-v3-stage-install.py -v`: all 8 tests pass in 19.040s.
- A bounded Linux counterfactual invokes actual configure through bash with a
  colon-bearing source alias and reproduces the auxiliary-file error. A controlled
  conversion-output boundary then executes the fixture's Windows branch, real
  configure, libtool compile/link, recursive installs and installed production
  header checks: all 8 cases pass in 18.199s. Recorded argv uses absolute
  `/usr/bin/bash`, the converted source alias and unchanged `--prefix=D:/ci/prefix`.
  Linux has no MSYS cygpath; only its output is controlled in this probe. This
  proves local argument handoff and real fixture behavior, not native Windows.
- Python AST syntax, all eight original test-method ASTs and `git diff --check`
  pass. Production Makefile.am, src/Makefile.am and installed-consumer driver are
  byte-for-byte unchanged. CLAUDE.md documents C++ cpplint, which does not apply
  to this Python-only change; no Python lint command is documented.

The initial local boundary probe used a POSIX source alias containing spaces;
configure rejected its unsafe srcdir. The corrected probe uses hyphenated source
aliases and retains the failed setup log. No product change addressed that probe
setup. Exact commands, source SHA-256 bindings, argv receipts, tool versions and
limits are in the primary checkout's literal journal
`.debug/ci-windows-stage-fixture-qf96yslm/journal.md` and `implementation/`.
No full baseline, runtime/provider/dependency change, old platform diagnostic,
CI retry/cancellation, validation loop, merge or publication ran. Native Windows
checks remain incomplete and CI/v3-PR owned under AGENTS.md.

Groundwork validation passed at `929324c216ff713e5232773589d82dfddfb37af5`
(1 iteration, 0 fixed, 0 unworked): seven fresh approvals plus justified spec and
architecture carries cover all applicable profiles. The conventions approval
covers all CI repair changes since c0 against primary checkout CLAUDE.md.
Corrected native Windows execution remains incomplete and CI/v3-PR owned.
