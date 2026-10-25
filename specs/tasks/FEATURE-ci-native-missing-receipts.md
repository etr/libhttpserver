# FEATURE-ci-native-missing-receipts: four native diagnostic receipts

Status: implementation prepared; caller review and the single approved CI dispatch pending.
Diagnostic branch only. **Never merge or cherry-pick this branch into v3.**

Base: `v3` at `37095c23751960b6e1adcad95d7b3c4c34c64351`.
Branch: `task/FEATURE-ci-native-missing-receipts`.
Worktree: `.worktrees/FEATURE-ci-native-missing-receipts`.
Plan: primary checkout `.groundwork-plans/FEATURE-ci-native-missing-receipts-plan.md`.
Journal: primary checkout `.debug/ci-native-missing-receipts-8l2nxp1a/journal.md`.
Preparation receipts: the journal's sibling `implementation/` directory.

## Authorized scope

The user's 2026-10-25 approval permits one manual CI batch, exactly once per target:

| Native host and selected context | Original target | Allowance |
| --- | --- | --- |
| windows-2022, MSYS2 MINGW64 clang/libstdc++, TLS off/shared | package consumer `no external`, unchanged original traffic/configuration checks | 30 seconds |
| Same Windows lane | original `native_http1_e2e`, 62 tests | 190 seconds including diagnostics/retirement; original 180-second failure gate retained |
| macos-15, selected Homebrew LLVM/libc++/SDK and private OpenSSL 3.5.9 | original `tls_psk`, 14 tests | 60 seconds including diagnostics/retirement |
| Same Darwin lane | original `quic_interop_tls`, 4 tests | 60 seconds including diagnostics/retirement |

Maximum four target invocations and 340 allocated target seconds. A failed target
must not retry or prevent the other selected target in its job. Setup failures
stop their own job with zero target invocations. Each job has a 90-minute setup/
execution ceiling. The executor has run **zero original target invocations**.
The original traffic, Consumer, configuration checks, native 62-test source,
QUIC 4-test source, PSK assertions/thresholds and root-open security flags retain
their behavior. Three original PSK connect/exchange assertion arguments are
stored once in booleans so the diagnostic and assertion share one evaluation.

## Implementation

- [x] Replace only this branch's already registered package workflow with manual
  dispatch and two native jobs; retain pinned required checkout/MSYS2/toolchain/
  artifact setup. Require the caller-reviewed exact SHA, branch identity and first
  run attempt. Fetch history for the approved-base proof. No matrix driver,
  nonselected suite, global baseline or `make check` invocation.
- [x] Build only the selected library/lane and named programs. Windows uses the
  installed shared consumer/native integration compilation and original source
  inventory. Darwin uses the original two named make targets. Keep O0/debug,
  provider flags, original privilege-selection preflight and original working
  directory choices. The port-443 bind is execution setup, not a target case.
- [x] Charge the full per-target allowance and attempt to a flushed durable ledger
  before launch. Parent/target-bound permits are exclusive and single use;
  existing/abandoned budgets refuse restart. Close after independent outcomes.
- [x] Reuse the exact bounded WSAPoll trace and destructive-call fixture from
  `5cf69d11f93b4760a6e863e392a3eead323f55e1` selectively, without its branch,
  16-attempt runner or report. Save WSA error immediately after the original call,
  retain original exception/exit 6 and trace bounded stop/dispatch/socket state.
- [x] Preserve raw stdout/stderr separately. Distinguish primary exit from EOF;
  retain primary creation identity, owned job descendant identities and handle
  counts. Snapshot after a delayed summary, primary exit before EOF or before
  the original gate. Retire only the existing held job/unreaped process group.
- [x] Discover existing CDB for an optional bounded, single-attempt, PID-scoped
  `-pvr` stack capture without suspension; retain `-pd`, local symbols and `qd`.
  It runs inside the target allowance and before the original gate, never
  reruns a target, and records absent/incomplete tools honestly. No system or
  TLS-enabled memory dumps. Process/EOF snapshots remain independently available.
- [x] Capture PSK version/default-host/SNI, held predicates, stored outcome,
  owner-thread `ERR_peek_error()` and original operation timing without another
  handshake, connect, cancel, exchange or wait. The actual QUIC root-open owner
  is `test/support/quic_interop_files.cpp`: save errno before logging/metadata,
  record bounded root/temp allowlist/lstat/type/uid data, preserve the original
  open flags/path and throw.
- [x] Strip target/tool environments to an allowlist; record actual compiler/
  SDK/provider/runtime import selection, configuration/source/tool/library
  hashes and setup commands. Reject an unavailable Darwin SDK discovery or a
  provider differing from 3.5.9 before targets. Upload receipts/configuration,
  not full native build/provider trees.

## Preparation verification

- [x] 23 offline contracts pass, including durable prelaunch charges, no resets,
  single-use permits, independent failures/no retries, missing/malformed receipts,
  original 180-gate behavior, actual fake-child primary-exit/EOF separation and
  descendant retirement, raw pipes, no extra stop on traffic failure, retained
  independent shutdown 6, secret exclusion and destructive Winsock observations.
- [x] GNU and LLVM/libc++ compile the two modified Darwin translation units using
  cached OpenSSL headers, without linking/running their original programs.
- [x] Cached genuine LLVM-MinGW compiles the modified original consumer to a PE
  object, without running it. This compiler/runtime differs from CI MSYS2 and
  proves compilation only.
- [x] Cached actionlint, Python syntax, changed C++ cpplint and diff checks pass.
  No separate Python style linter is installed; the Python code was reviewed
  against the adjacent stdlib script conventions. No tool was bootstrapped.
- [x] Byte contracts preserve original traffic/process/configuration modules and
  the 14/4/62 test counts, assertion ordering and single-evaluation operations.
- [ ] Caller full Groundwork validation and readiness seal.
- [ ] Caller push and **one** manual dispatch with `approved_sha` equal to its
  validated diagnostic commit; collect/close both job ledgers once. No fallback
  workflow, rerun, branch merge or production repair is authorized here.

## Limits and interpretation

Actual Windows/MSYS2 and Darwin runtime observations remain unexecuted until the
caller dispatch. Python Windows job/identity APIs and optional CDB are syntax-
reviewed but cannot be proved by Linux compilation/offline fixtures. Native CI
will report unavailable APIs/tools explicitly. Local cached OpenSSL/compiler
receipts do not establish the future CI toolchain identity; CI records that
identity anew. No original native check is presented as passed.

All stderr output, clocks, memory snapshots and optional stack capture perturb
observation timing. `ERR_peek_error()` describes the fixture owner's current
provider queue, not another worker thread's queue. For `cancel-held`, `elapsed_us`
is the original longest owner tick; for timeout rows it is elapsed since submit;
for mixed-host rows it is the single original connect/exchange duration. The
host label is the configured default plus the client SNI; it is not a newly
inferred internal host-selection result. Failure/nonreproduction remains UNKNOWN
unless the captured values justify a narrower conclusion.

The separately assessed new Darwin TLS-off/static external fixture failure is
outside this four-target authorization. It is not instrumented, executed or
repaired here. Existing bauth work and retained diagnostic refs remain untouched.
