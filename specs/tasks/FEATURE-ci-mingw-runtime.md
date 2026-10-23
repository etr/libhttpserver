# FEATURE-ci-mingw-runtime: recognize the MinGW SEH compiler runtime

Status: Implemented; pending caller validation and finalization.
Base: `v3` at `f91dddf4faf21f33cf27e9e4ece3870e3a593e56`.

Windows package job `120444167361` in run `39962619327` and archive job
`120444991245` in run `39962644789` reach the native audit, pass A1, then
fail A2 on `libgcc_s_seh-1.dll`. The existing `libgcc_s.*|libgcc_s-*`
patterns omit this underscore spelling. The native audit now recognizes
that exact runtime basename, retaining the existing dependency policy.
The [official MSYS2 runtime package](https://packages.msys2.org/packages/mingw-w64-x86_64-libgcc?repo=mingw64)
lists `/mingw64/bin/libgcc_s_seh-1.dll`; the failed job installed that package.

Action items:

- [x] Create an isolated Groundwork worktree from the exact v3 base;
  preserve the bauth worktree and previous closed diagnoses.
- [x] Retain both Windows job logs and bind the failed/local audit, recipe,
  fixture and package workflow sources to the recorded base revision.
- [x] Add a test executing the production make recipe and real native audit
  against the observed runtime import in TLS-off and TLS-on `.exe` lanes;
  observe the original A1 PASS / A2 rejection before editing production.
- [x] Add only the exact `libgcc_s_seh-1.dll` runtime classification.
- [x] Reject near-miss extensions, versions and unknown underscore runtimes
  in both TLS modes; preserve existing provider and MSYS rejection controls.
- [x] Keep C++ linker, source scan, executable suffix, PE validation, missing
  artifacts/inspectors and archive/static-symbol checks unchanged.
- [x] Keep the stronger installed-package auditor, workflows, compiler
  selection, dependencies and engine unchanged.
- [x] Run the focused native-linkage module, Bash/Python syntax and diff checks.
- [x] Retain raw RED/GREEN commands/results, source bindings, documentation
  reference and journal under main `.debug/FEATURE-ci-mingw-runtime/`.
- [ ] Caller: Groundwork validation and local finalization into v3.
- [ ] CI / v3 PR: native Windows package and archive build/audit evidence.

Local verification: the original 17-test focused baseline passed. The new
make-gate regression failed in both TLS modes on the exact observed DLL,
with A1 passing and A2 reporting the same rejection as CI. After the exact
basename correction, all 19 tests pass. Three rejected runtime lookalikes
are exercised in both TLS modes. Existing empty/`.exe` recipes, OpenSSL
mode handling, C++ linker/build failure propagation, static provider,
missing artifact, alternate provider and MSYS controls still pass.
Bash syntax, Python AST syntax and `git diff --check` pass. The documented
C++ linter does not apply to these script changes; no Python linter is
documented in CLAUDE.md.

The tests execute GNU make, the production recipe and the real source audit;
build artifacts and native inspector outputs are controlled fixtures.
They do not compile or inspect a native Windows binary. The installed-package
auditor already recognizes selected-prefix `libgcc_s_*.dll` and remains
byte-for-byte unchanged. Only one production dependency case is extended;
all other audit guards remain unchanged.

Native Windows CI remains incomplete and owned by CI / v3 PR under AGENTS;
missing nonlocal evidence does not block local completion. No full native
baseline, installation, unrelated repair, validation loop, merge or publication
was performed. Linux parity and Darwin header-deadline causality remain
CLOSED/UNCERTAIN; neither diagnosis was reopened.

Project notes: focused script checks need Python 3, Bash and GNU make, with
no installation. Full project builds use Autotools/C++20 (`bash bootstrap`,
out-of-tree configure, make); they were outside this focused repair.
Groundwork identity helper returned the task path/branch verbatim.
Git metadata writes require sandbox escalation.
