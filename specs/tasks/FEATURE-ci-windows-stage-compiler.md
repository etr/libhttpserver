# FEATURE-ci-windows-stage-compiler: retain the native fixture compiler diagnostic

Status: In Progress — diagnostic checkpoint ready; native compiler cause and repair pending.
Base: `v3` at `6fc672326b9cad8f4aa068b9641fc03e21218e31`.

The fresh native Windows package job `120607477636` and archive job
`120608161470` reach the fixture's C compiler check, then report that the compiler
cannot create executables. The fixture's configure stdout omits the actual
compiler/linker stderr, and class cleanup removes its temporary `config.log`.
The existing Windows package artifact `12526449139` retains the successful outer
configure log, but not the failing inner fixture log. The native cause is unknown.

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
- [ ] Caller: review checkpoint, publish only this task branch, and dispatch one
  manual Windows-only native control using `windows_only=true`.
- [ ] Read the actual native diagnostic and adjudicate the cause. If the control
  passes, record intermittency/uncertainty and stop; do not retry unchanged.
- [ ] Resume this same task for a proven minimal repair with relevant TDD.
- [ ] Remove every temporary workflow change, restoring the exact base workflow
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
No native Windows execution, compiler/flag/runtime repair, full validation,
publication or merge is claimed at this checkpoint. Native capture, the accepted
repair, workflow restoration and final checks remain incomplete. Other platform
failures and earlier bounded investigations are outside this checkpoint.
