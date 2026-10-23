# FEATURE-ci-native-link-map

Status: Complete (local implementation, Groundwork validation and finalization).
Native platform results remain CI / v3 PR owned; no release readiness claimed.

Scope: repair the installed-consumer link-map option rejected by Darwin ld.
Base: v3 at `808f7fa20b0bc2f3c3049140b86189e704c5378e`.

- [x] Create an isolated task worktree using the Groundwork identity helper.
- [x] Reproduce the command defect with a failing compile-probe regression.
- [x] Select Darwin `-Wl,-map,` while retaining GNU `-Wl,-Map,` elsewhere.
- [x] Verify the unchanged map receipt path, ordered library arguments, quoted
      compiler wrapper/options, compile-option defaults/overrides, source and
      dependency hashes, artifact hash, and dependency contamination rejection.
- [x] Run affected Python module tests and a real local GNU compile/link/run.
- [x] Check the diff and Python syntax; record unavailable native evidence.
- [x] Caller: complete Groundwork validation and finalization.
      Three focused reviewers approved with no findings; affected evidence reused.
- [ ] CI / v3 PR: obtain fresh native Darwin installed-consumer and selected
      archive results after merge; retain other platform results honestly.

## Failure evidence and causal assessment

The original native macOS failure is retained at
`/tmp/v3-ci-job-120361073477-direct.log`, lines 299–318. Darwin ld rejects the
unconditionally emitted GNU `-Map` option in `compile_probe`. The earlier TCP443
permission probe is followed by the existing privileged setup; it is not the
terminal failure. The caller also retained the same failure from selected-archive
job 120362794854 at `/tmp/v3-ci-job-120362794854-direct.log`.

The focused regression ran against the original implementation first:
`python3 scripts/test-v3-installed-package.py CompileProbeTest -v` failed only
the two Darwin option variants, showing `-Map` instead of `-map`. Linux,
FreeBSD and Windows command variants retained their expected GNU syntax.

## Local verification

- The focused command regression passed after the minimal platform selection.
- `python3 scripts/test-v3-installed-package.py -v`: 37 tests passed.
- `python3 scripts/test-v3-native-benchmark.py`: 18 tests passed, including the
  existing compile-probe default/override contract.
- A bounded real `compile_probe` invocation compiled and linked a C++ consumer
  against a temporary installed-header fixture with GNU c++, ran the resulting
  executable, and verified the receipt's map file existed (20,637 bytes).
  The temporary directory contained spaces; dependency parsing also passed.
  This is a linker smoke check, not installed-library acceptance evidence.
- `python3 -m py_compile scripts/lib/package_integration.py
  scripts/test-v3-installed-package.py` and `git diff --check` passed.
- CLAUDE.md prescribes cpplint for C++; no C++ files changed and no Python
  linter is configured. No local Python linter was available.

Darwin ld / ld64.lld was unavailable locally. Native Darwin linking and the
fresh full package matrices remain incomplete until CI executes them. No broad
native/build baseline was rerun, and no previous failed receipt was promoted to
a pass. Prior validation closures remain unchanged. No product sources, audit
policy, dependency/provider/client pins, tags or releases changed.

## Implementation notes

These Python script tests use standard-library unittest and need no dependency
installation. Existing modules load `scripts/lib` through `sys.path`; use
`platform.system()` for native host selection. The repository's general C++
setup remains `./bootstrap`, an out-of-tree configure/build and `make check`.
The isolated task branch was created from v3 using the helper-returned identity;
Git metadata writes required sandbox escalation. The unrelated bauth worktree
was preserved.
