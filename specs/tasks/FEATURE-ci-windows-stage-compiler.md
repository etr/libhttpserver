# FEATURE-ci-windows-stage-compiler: keep libtool flags out of nested compiler checks

Status: In Progress — native cause captured; minimal repair awaiting final Groundwork validation.
Base: `v3` at `6fc672326b9cad8f4aa068b9641fc03e21218e31`.

The fresh native Windows package job `120607477636` and archive job
`120608161470` reach the fixture's C compiler check, then report that the compiler
cannot create executables. The fixture's configure stdout omits the actual
compiler/linker stderr, and class cleanup removes its temporary `config.log`.
The existing Windows package artifact `12526449139` retains the successful outer
configure log, but not the failing inner fixture log. The single diagnostic control
`40017021624` / job `120631601349` now proves that clang rejects the inherited
libtool-only `-no-undefined` option before any staging cases run.

The first checkpoint reports the last actual configure check before cleanup,
excluding cache/output-variable sections and the failed source dump. Long check
output retains its first and last 1000 bytes with a truncation marker. This keeps
the command and diagnostic near the end of the exception for the installed
matrix's final-3000-byte error report. Configure failure still raises
`RuntimeError`, and normal temporary-directory cleanup still runs.

Action items:

- [x] Read primary CLAUDE.md/AGENTS.md, saved plan, literal journal, complete fixture,
  existing workflow, and retained outer Windows configure/command context.
- [x] Create the isolated Groundwork worktree from the exact v3 base; preserve
  unrelated work.
- [x] Prove RED with actual configure and a deterministic invalid clang option:
  compiler stderr exists in `config.log` but is absent from the original exception.
- [x] Add bounded failure reporting before cleanup; preserve the original failure.
- [x] Prove GREEN with actual configure, including an over-budget real compiler
  diagnostic; retain compiler command/stderr in the final 3000 output bytes.
- [x] Preserve all eight original test-method ASTs, configured `D:/ci/prefix`, MSYS
  shell/path handoffs, real libtool operations, and production Makefile drivers.
- [x] Run the healthy eight-case fixture and affected syntax/workflow checks.
- [x] Prepare a temporary branch-only `windows_only` manual dispatch input in the
  existing package workflow; keep the Windows job and normal push/PR behavior
  unchanged. The three other jobs are explicitly skipped for that selection.
- [x] Caller: review checkpoint, publish only this task branch, and dispatch one
  manual Windows-only native control using `windows_only=true`.
- [x] Read the actual native diagnostic and adjudicate the cause. If the control
  passes, record intermittency/uncertainty and stop; do not retry unchanged.
- [x] Resume this same task for a proven minimal repair with relevant TDD.
- [x] Remove every temporary workflow change, restoring the exact base workflow
  before final validation and merge into v3.
- [ ] Caller: complete required Groundwork validation and finalization.

Checkpoint verification:

- Bootstrap passes with the existing .PHONY / AC_OUTPUT warnings.
- Original healthy fixture: all 8 tests pass in 19.173s.
- Actual-configure RED fails because the emitted exception omits clang's
  `unknown argument` diagnostic; the original `RuntimeError` and cleanup are
  observed. GREEN retains the actual command and compiler stderr in the final
  3000 bytes; the same proof with 80 invalid options verifies bounded truncation.
- Changed healthy fixture: all 8 tests pass in 20.183s.
- Python syntax, original eight test ASTs, unchanged setup outside failure
  reporting, unchanged MSYS handoffs and production driver bytes pass.
- All 4 existing package workflow tests pass. Actionlint 1.7.7 passes with its
  optional shellcheck integration disabled (shellcheck is unavailable locally).
  `bash -n` passes for all 18 existing shell step bodies. Parsed workflow
  comparison confirms the entire Windows job, action pins, privileges and normal
  workflow are unchanged apart from the temporary dispatch/job selectors.
- `git diff --check` passes. CLAUDE.md's C++ cpplint does not apply to these Python,
  YAML and task-record changes; no Python linter is documented.

Evidence and the task-local executable probes are retained in the primary
checkout at `.debug/ci-windows-stage-compiler-lbpdh7hb/implementation/`; the literal
journal is `.debug/ci-windows-stage-compiler-lbpdh7hb/journal.md`.
These results describe the diagnostic checkpoint. The repair evidence below
supersedes its pending native-capture and flag-repair items. Other platform
failures and earlier bounded investigations remain outside this task.

Native capture and minimal repair (2026-10-23):

- The single caller-owned control `40017021624`, source `07cb8c9e`, failed in
  Windows job `120631601349`. Its real compiler check was
  `clang -std=gnu23 -lws2_32 -lmswsock -lbcrypt -lpthread -no-undefined conftest.c`;
  clang reported `unknown argument '-no-undefined'`. Zero staging cases ran.
  The terminal failed-job log is `/tmp/v3-ci-win-diag-run40017021624-log-failed.txt`.
- Verified cause: the installed-package driver supplies `LDFLAGS` in the outer
  make environment, including when empty. GNU make replaces that inherited value
  with its configured assignment and exports it to the Python fixture. The fresh
  nested configure consequently receives the libtool-only option as compiler
  LDFLAGS. This is established by actual configure/make/clang RED/GREEN, rather
  than a compiler simulation or a guessed flag workaround.
- `configure.ac` now keeps only `-lpthread` in the MinGW, MSYS and Cygwin
  additional-library assignments. Windows socket/bcrypt flags and pthread
  linkage remain. The existing aggregate `libhttpserver_la_LDFLAGS` still owns
  `-no-undefined`; a real configured libtool-link dry run confirms it remains.
- The task-local `implementation/configured-flags-probe.py` executes real outer
  configure with the Cygwin host arm on Linux, using `CC='clang -std=gnu23'`,
  `CXX=clang++` and `LDFLAGS=''`, then invokes production
  `make check-v3-stage-install`. RED exports `-lpthread -no-undefined` and fails
  at the identical real clang diagnostic before any test runs. GREEN exports
  only `-lpthread`; the unchanged actual eight-case fixture passes in 20.460s.
  This tests flag inheritance locally; it is not a native Cygwin/Windows runtime
  result. Inputs/source hashes, outer configure logs, exported flag snapshots,
  fixture output and the libtool-link dry run are retained beside the probe.
- The temporary workflow is restored byte-for-byte to base `6fc67232`.
  Bootstrap passes with the existing warnings. All 4 package workflow tests,
  actionlint 1.7.7 (optional shellcheck unavailable), all 18 shell-body syntax
  checks, Python syntax and diff whitespace pass. Exact comparison confirms the
  checkpoint fixture, all eight test bodies and production Makefile/package
  drivers are unchanged by this repair. No C++ source changed; C++ cpplint does
  not apply.

Implementation is ready for caller-owned final Groundwork validation. No second
native CI run, broad baseline, full validation, publication, merge or completion
is claimed. The actual post-repair Windows/platform matrix remains CI/v3-PR
owned under the local-completion policy. Earlier Darwin/Linux/Tpo assessments
remain closed and uncertain where already recorded; none was reopened.
