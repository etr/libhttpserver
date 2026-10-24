# FEATURE-ci-darwin-python-host-fixtures

Status: In Progress
Base: v3 `cf15f14590672c8fb461147635da3f535238692f`
Plan: `.groundwork-plans/FEATURE-ci-darwin-python-host-fixtures-plan.md`
Journal: `.debug/ci-6b2-darwin-python-assertions-wrzyeazd/journal.md` in the primary checkout.

Correct the four established Python fixture assumptions from Darwin archive
40072260645 / job 120808170836, preserving all production validation policies.
The archive reported 232 passes, two skips and five Python module failures.

- [x] Canonicalize the valid restored-corpus fixture root; retain restored input,
  all seed bytes, seed 987, reproduction and deliberate symlink rejection checks.
- [x] Resolve the caller-selected make executable before fixture `os.execv`;
  exercise inherited selection and a real PATH-selected `MAKE=gmake`, retaining
  both TLS modes and exact test-enumeration calls.
- [x] Require retained compiler argument aliases and their resolved target bytes;
  exercise the real orchestrator, child, make and retention under symlinked TMPDIR.
- [x] Replace both host `/usr/bin/timeout` assumptions with controlled fixture
  executables; retain the exact selected deadline command, 180-second argument,
  failed native case logs and frozen receipt checks. Explicitly verify missing
  deadline selection fails before any make command. These fixtures establish
  command construction and availability, not native GNU timeout timing.
- [x] Enrich the unchanged strict static-archive assertion with compiled dependency
  hashes, source/package/archive identities, recorded commands, depfile contents
  and selected/foreign library artifact hashes for future required CI evidence.
- [x] Complete bounded local fixture verification, Python compilation and diff checks.
- [ ] Complete caller-owned validation/finalization.

## Verification and limits

RED reproduced corpus-symlink rejection, the unrequested raw compiler-target
catalog entry, basename `os.execv` failure, and both missing-host-deadline failures.
The corresponding focused GREEN checks pass. Final affected-module runs all
passed locally, without skips:

| Command (python3) | Tests | Retained log |
| --- | ---: | --- |
| test/integ/protocol_fuzz_runner_test.py | 13 | final-fuzz.log |
| scripts/test-v3-platform-ci.py | 20 | final-platform.log |
| test/integ/release_gate_runner_test.py | 47 | final-release-gates.log |
| scripts/test-v3-packaged-adapters.py | 42 | final-adapters.log |
| test/integ/release_artifact_test.py | 15 | final-release-artifact.log |

All five changed Python modules passed `python3 -m py_compile` and
`git diff --check` passed. No Python linter is documented; the project's cpplint
command applies to C++ sources, which this task does not change. Logs are retained
in the task worktree at `build/ci-darwin-python-host-fixtures/`. Local success is
not native Darwin evidence, and does not close its unknown adapter failure.

The adapter's Darwin `private or foreign dependency` failure remains UNKNOWN and
its native gate INCOMPLETE. Its fixture root was already canonical; the native
receipt did not retain the unexpected dependency input. The assertion still
requires `ValueError` matching `static archive`; real compiler/archive/link-map
negative and positive controls remain unchanged. Diagnostic context is evidence
only and does not establish a fix. Next required CI on the approved correction
head owns native input capture. No standalone Darwin adapter replay is authorized.

Darwin, Windows and FreeBSD checks remain CI/v3-PR owned and unexecuted locally.
Remote 6b2 CI is preserved; no push, cancellation or publication is part of this
implementation. The prior epoll batch exhausted its 64-execution budget, remains
CLOSED with unknown causality, and receives no additional execution or probes.
The older closed investigations remain closed. No production library, runner,
workflow, build configuration, identity or deadline policy is changed.
