# FEATURE-ci-native-linkage-exeext: respect configured executable suffix

Status: Complete (local implementation, validation and finalization).
Base: `v3` at `794ecc08f86e72469eed791c6b13d5a5490cefb4`.

Windows package job `120420001573` in run `39955436208` and archive job
`120420935368` build the library, then fail because the native linkage gate asks
make for `native_linkage`, while Automake exposes `native_linkage.exe`.
The recursive target now uses `native_linkage$(EXEEXT)`, retaining the empty
suffix on Unix and using the configured suffix on Windows.

Action items:

- [x] Create an isolated Groundwork worktree from the recorded v3 head;
  preserve the bauth worktree and prior closed repairs.
- [x] Retain both original native Windows logs and bind the relevant unchanged
  Makefile/audit inputs to the failed and local base revisions.
- [x] Add a regression executing the actual production recipe with GNU make
  for empty and `.exe` suffixes; observe the original missing-target failure
  before editing production code.
- [x] Respect configured `EXEEXT` in the recursive native linkage target.
- [x] Verify both suffixes reach the real source/header and artifact audit;
  preserve configured TLS mode, C++ linker rejection, build failure propagation
  and provider rejection.
- [x] Preserve the complete production inspector and all existing adversarial
  tests, including static symbols, missing archive and Windows import controls.
- [x] Run the affected 17-test module, bootstrap, out-of-tree TLS-off configure,
  generated-target dry runs for both suffixes, Python syntax and diff checks.
- [x] Retain raw commands, results, source bindings and journal under main
  `.debug/FEATURE-ci-native-linkage-exeext/`.
- [x] Caller: Groundwork validation and local finalization into v3.
- [ ] CI / v3 PR: native Windows package and archive builds and audits.

Local verification: the five new make-gate tests and all 12 existing inspector
tests pass. The original `.exe` RED fails with `No rule to make target
'native_linkage'`; empty suffix behavior passes before and after the change.
The regression reads the production recipe from `Makefile.am`, executes it
with real GNU make, and runs the unchanged production audit against the real
source tree. Small build Makefiles and inspector outputs are controlled
fixtures; they do not compile or inspect native Windows binaries. TLS-on
fixtures accept OpenSSL while TLS-off fixtures reject the same dependencies.
The C linker, failed build and static provider cases fail the gate as required.

Bootstrap and configure pass with existing Automake/Autoconf warnings and
missing optional documentation tools recorded in the logs. Generated test
Makefile dry runs resolve both `native_linkage` and `native_linkage.exe`; the
unbuilt v3core archive is explicitly excluded from those dry runs with `-o`.
These are target-resolution checks, not binary build evidence. The production
audit bytes and entire original `AuditFixtures` class are unchanged. Python
scripts have no documented standalone linter; no C++ files changed.

Native Windows execution remains incomplete and CI / v3 PR owned under
AGENTS. No full native baseline, unrelated repairs, CI reruns, validation loop,
merge or publication was performed. Older Linux parity causality remains
UNCERTAIN after its previously closed bounded diagnosis. Prior production
unwind/entropy fixes, audit policy and nonlocal checks remain untouched.

Project notes: Autotools/C++20 setup uses `bash bootstrap`, out-of-tree
`configure --disable-v3-tls --disable-examples`, then make. This task uses only
targeted Python unittest, GNU make fixtures and configure/dry-run checks.
The worktree identity came verbatim from the Groundwork helper; Git metadata
writes required sandbox escalation.
