# FEATURE-ci-native-temp-file-fixtures: native temporary fixture parents

Status: Implementation complete; caller validation and finalization pending.
Base: local `v3` at `d70b89e04a32d2bef5271d40839c2d9db468f029`.
Plan: `.groundwork-plans/FEATURE-ci-native-temp-file-fixtures-plan.md`.

## Goal and scope

Repair the proven invalid `/tmp` assumption in the two owning native HTTP test
fixtures. Use the native system temporary directory with literal unique creation
and actual creation errors. Preserve contents, descriptor close, ownership,
lifetime and cleanup, and all original one-shot and multipart assertions.
No production behavior or platform skips change.

## Action items

- [x] Read the saved plan, project instructions and prior executor memory; create
  the isolated task branch from the exact local v3 base.
- [x] Add actual filesystem regressions for both helpers: native parent identity,
  simultaneous unique names, binary content/rewrite and cleanup. Directory
  contents use a literal filename containing spaces and a semicolon.
- [x] Observe RED with process-local TMP/TEMP/TMPDIR pointing to an existing
  directory containing spaces: two tests, 24 checks, four parent-identity
  failures from the original hardcoded helpers.
- [x] Derive both patterns from `std::filesystem::temp_directory_path()`;
  retain mkstemp/mkdtemp exclusive creation and cleanup. Creation failures throw
  `std::system_error` with errno, operation and attempted path; discovery errors
  propagate without a fallback.
- [x] Verify GREEN under the same controlled environment: the two fixture tests
  and three original owning HTTP cases pass, five tests / 122 checks / no skips.
- [x] Verify all three original HTTP case bodies remain byte-identical and the
  controlled parent is empty after process exit.
- [x] Verify GNU compilation/linkage, existing LLVM/libc++20 owning TU syntax,
  cpplint on both changed C++ files, and `git diff --check`.
- [x] Persist receipts in the primary journal tree before handing off.

## Verification and limits

Primary journal: `.debug/ci-d70-windows-execution/journal.md` in the main checkout.
Raw commands, controlled environment, exits, output, timings and retained binaries:
`.debug/ci-d70-windows-execution/temp-fixture-implementation/`.

The retained 5,920,268-byte GNU core archive was verified with SHA256
`96ec5c852ff311bc2c1053098f11580670f1f76c90bcacba42a74805eff9c820`.
Production/build inputs match the archive's 87f7 source through d70; this task
changes only test helpers/tests. No core rebuild, configure or full baseline.
The task-local selector includes the actual owning TU and checks the selected
case count. The original cases executed were:
`one_shot_definition_serves_once_then_500`,
`multipart_client_abort_removes_partial`, and
`multipart_disk_upload_round_trip`.

Two compile setup failures are retained: missing parity response parser linkage,
then an incorrectly named parity source. Corrected linkage uses the repository's
`test/parity/response_frame.cpp` from the owning Automake target. Initial cpplint
rejected the C++17 filesystem include; the existing C++20-specific include
annotation convention resolves it. These were local setup errors, not runtime
regressions.

Native Windows post-fix execution remains CI-owned and incomplete. Local Linux
results and Linux LLVM/libc++ syntax do not prove native Windows execution.
Post-summary 180-second hang remains CLOSED/UNKNOWN. Distinct archive WSAPoll
exit 6 remains CLOSED/UNKNOWN with numeric error/registration evidence missing.
The closed kqueue 25, epoll 64, Darwin-header and clock/adapter scopes remain
unchanged. No new native probes, timeout changes, process-exit masking, polling
repairs, full suite, publication, merge or cleanup were performed.
