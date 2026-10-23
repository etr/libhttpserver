# FEATURE-ci-macos-unwind: selected Darwin LLVM unwind runtime

Status: Implemented; local focused checks pass. Caller-owned Groundwork validation
and finalization are pending. Native macOS package/archive execution remains
CI / v3 PR owned. Base: `v3` at
`6a1709daf6d5b51dac18a85c50030aeddd4cc570`.

Mac package job120396472543 in run39948363870 resolved LLVM23 libc++ and
libc++abi dependencies on `lib/unwind/libunwind.1.0.dylib`, then rejected both
edges as undeclared during the first private strict aggregate audit. The
original bounded diagnosis and native log remain unchanged at root
`.debug/FEATURE-ci-macos-unwind/run-taafd3p6/`.

The compiler selector now exports `V3_PACKAGE_TOOLCHAIN` from the same LLVM
prefix that supplies its compiler and C++ runtime. Both Mac workflows pass
that selection explicitly via the existing `--toolchain-prefix` option, so
privileged execution retains it even when sudo filters the environment.
Darwin runtime classification accepts only numerically versioned libunwind
dylibs whose resolved parent is the canonical selected prefix's `lib/unwind`.
Selected-prefix symlinks and dylib aliases to those files work; other directories,
versions, malformed names, unrelated libraries and symlink escapes reject.
Global runtime patterns, dyld-cache leaves and graph inspection remain unchanged.

Action items:

- [x] Create the isolated Groundwork worktree from the recorded v3 head;
  preserve the Windows entropy repair, bauth worktree and other prior fixes.
- [x] Capture failing actual graph-policy tests reproducing the two native edges
  and failing compiler/workflow propagation tests before production edits.
- [x] Export the selected LLVM prefix and pass it explicitly through both Mac
  package and selected-archive workflow commands, including their sudo paths.
- [x] Restrict Darwin unwind classification to the canonical selected directory
  and numeric versioned basename; preserve strict provider and forbidden-library
  checks, real-file inspection, hashes and transitive traversal.
- [x] Refute trust-boundary doubts using canonicalization/escape/negative controls,
  executed workflow shell blocks with a cleaned privileged environment, actual
  release dispatcher forwarding, and graph/hash/provider regressions.
- [x] Run affected audit, platform, package-workflow and release-workflow tests;
  actionlint, Python compilation, Bash syntax and diff checks pass.
- [x] Retain raw RED/GREEN logs, corrected fixture setup, doubts and command
  results under root `.debug/FEATURE-ci-macos-unwind/repair-selected-prefix/`.
- [ ] Caller: Groundwork validation and local finalization into v3.
- [ ] CI / v3 PR: genuine native macOS package and selected-archive execution.

Local verification: 40 installed-package policy/provenance tests, 7 platform
execution tests, 4 package-workflow tests and 14 release-workflow contract tests
pass. The new workflow test executes the actual Mac shell command blocks in
both direct and simulated environment-cleaning sudo paths, with substituted
native tool boundaries; it checks selected compiler and prefix arguments.
These are policy/propagation fixtures, not native macOS installed-package proof.
No full native baseline, CI rerun, Linux parity investigation, validation loop,
merge or publication was performed. Native gates remain incomplete until CI.

Project notes: Autotools/C++20 uses bootstrap, out-of-tree configure and make.
This scripts-only task needs no dependency installation; focused tests use
Python unittest and the workflow suite's existing PyYAML dependency. Platform
fixtures substitute only native executable/process boundaries. Python files
have no documented standalone linter; changed-file `py_compile`, Bash syntax,
existing actionlint1.7.7 and `git diff --check` passed. Worktree branch/path came
verbatim from the Groundwork identity helper; Git metadata writes required
sandbox escalation. Agent memory was initially empty.
