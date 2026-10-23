# FEATURE-ci-darwin-header-deadline: Diagnose native external header deadline

Status: In Progress — diagnostic checkpoint only; awaiting native Darwin evidence.

Base: `v3` at `c0cfdabc8040273d60c610269a93d1c687ed8517`.

Plan: `.groundwork-plans/FEATURE-ci-darwin-header-deadline-plan.md` in the primary checkout.
Journal: `.debug/ci-darwin-native-69af4dxe/journal.md` in the primary checkout.
Prior closed diagnostic: `.debug/ci-darwin-header-deadline-ybz8bi7l/journal.md`.

## Goal and evidence

Determine why the installed shared TLS-off Darwin `external_http1_e2e`
`published_header_deadline_drives_empty_dispatch` case fails to observe peer close,
then repair only the proven cause. User authorized focused native diagnostics on
2026-10-23. Preserve the 300 ms header deadline, 10 s host failure deadline and all
existing assertions.

The current base reproduces the failure in archive run `39969790346`, Darwin job
`120469466623`, artifact `12509855171`: four tests, 52 checks, two failures,
10,006.1 ms for the deadline case. Assertions 217 (`receive_close`) and 221
(`!failed`) fail; assertion 219 (`empty_dispatches > 0`) passes. Other three cases
and the earlier native HTTP1 suite pass. The underlying cause remains unknown.

New native comparison before publishing the checkpoint: current c0 package job
`120468733495` passes the entire TLS-off lane, including this external deadline
case, then fails later in TLS-on public TLS with Python
`SSLCertVerificationError` (CA certificate missing key usage). Archive job
`120469466623` fails the header deadline at the same source revision, selected
LLVM toolchain and `-O0 -g`. Checkout versus extracted archive/execution context
is now an observed distinction; it is not an established cause. The CA issue is
queued separately and is outside this task.

## First native diagnostic result and remaining uncertainty

Native diagnostic run `39974555288`, job `120485650697`, passes all four existing
cases with zero failures using the exact c0 archive plus the live stderr trace
overlay. The header case takes 303.347 ms. Its actual timer sequence 8 fires,
applies result 0, resumes, matches the current deadline, enforces timeout,
releases the connection and closes the native socket successfully near 300 ms.
A snapshot with no deadline occurs between timer application and watchdog
resumption; a later host wait receives a wake. The original f91/c0 untraced
archive failures remain unexplained. Passing under tracing is neither a fix nor
proof that the failure is independent of this path.

Source review of that handoff finds registration retirement/reset under the
backend mutex, followed by notify; notify and wake acknowledgment share that
mutex. Snapshots borrow handles; dispatch-held leases end before the host returns
to waiting. No violated race invariant or decisive forced-interleaving regression
was established from this successful trace. The next checkpoint reduces trace
perturbation and records actual notify/signal/ack states to distinguish missing
wake from retained lease when a failure is observed.

## Required privilege context correction

The original c0 archive log (lines 294–325) and package log (lines 287–303)
show that binding port 443 failed for the runner, then the existing workflow
invoked the matrix through `sudo env PATH=...` with the exact checkout marked
Git-safe. Its build/test processes therefore ran as root. Both earlier
diagnostic drivers lacked sudo and did not record UID/GID; their runner-context
results are not like-for-like privilege evidence. Live tracing perturbation is
not the only established context contrast.

The existing artifact download/strict extraction/unchanged overlay remains in
the runner stage with its read-only GitHub token. A generated, retained Python
build script now runs through the existing sudo/env pattern, with both token
variables removed **before sudo**. Explicit selected compiler/runtime/temp paths
and exact Git safe.directory are passed; the provider bin directory follows the
existing archive platform PATH setup. The entire build/compile/inspect/test
command tree runs in that privileged stage. Its receipt records runner and
privileged UID/EUID/GID/EGID, cwd, compiler, provider, toolchain, PATH and build
flags. The build stage asserts EUID 0 and token absence. A final assert-and-exec
launcher records the actual test process UID/GID, cwd and selected runtime
search environment before replacing itself with the unchanged test executable;
its EUID must be 0 and its environment must exclude both token variables.
Immediately before native exec, that launcher restores each available SIGPIPE,
SIGXFZ and SIGXFSZ to SIG_DFL. This preserves the original direct subprocess
signal setup after Python startup would otherwise reintroduce ignored
signals. [Python subprocess documentation](https://docs.python.org/3/library/subprocess.html#subprocess.Popen)
defines this default `restore_signals` behavior. This is an execution comparator
correction, not an established explanation of the deadline failure.

Buffered runner-context run `39978609885`, job `120499691522`, subsequently
finishes successfully with four tests, 52 checks and zero failures/skips. It
emits 677 deferred records after the suite statistics; the header timer again
expires, resumes, enforces and closes normally around 303 ms with one retirement
lease. This is useful buffered-trace evidence in the runner context, and remains
incomplete comparison to the original root execution. It was allowed to finish
without cancellation or an unchanged retry.

The driver correction matches required execution setup and justifies an affected
native check. The caller reviews this driver-only change before publishing or
dispatching any further required run.

## Current checkpoint: unmodified original package prefix control

The corrected privileged diagnostic run `39982282514`, job `120512379909`,
passes four tests and 52 checks with zero failures/skips. The actual context
record confirms UID/EUID/GID/EGID 0. Its 677 deferred records again show healthy
header expiry, continuation, enforcement and native close around 303 ms, with
one retirement lease. This matches privilege/signal setup but establishes no
cause or fix: the core was still instrumented and the suite isolated from the
original package prelude.

The next single control uses the **unmodified** c0 source archive and original
`package_lane` implementation. Manual macOS checkout selects c0; push/PR keeps
its normal GitHub SHA. The inline driver remains bound to the diagnostic branch
workflow SHA, recorded separately from actual source HEAD. Existing
`selected_archive` reproduces the exact selected extraction, including its
original supplemental tooling/doc files and identity. No trace source overlay,
trace macro, test change or Python native-exec wrapper is used. Original core,
test and helper hashes must match the selected c0 descriptor throughout.

The privileged controller imports helpers from that extraction, records their
source hashes, asserts root/token-free context, and calls the original
`package_lane` directly for TLS-off. Original build/install, package checks,
consumer traffic/configuration and compiled-operation prelude run unchanged.
A driver-only suite selector retains the original first two private suite
definitions: `native_http1_e2e` and `external_http1_e2e`. A wrapper stops only
**after** original `run_suites` returns, including suite_result, required-case,
status and persisted suites.json validation. Original errors propagate unchanged.
The successful partial stop precedes static linkage, other suites and TLS-on;
it is explicitly not matrix gate passage or a behavior fix.

Only if the exact external-suite argv remains live after three seconds does a
driver-only subclass of original `_CommandTree.exited` collect one native
all-thread sample. It first uses the original non-reaping anchor, and samples
synchronously while the original owner reserves the PID and cannot concurrently
clean up/reap it. It never polls, waits, reaps, kills or replaces ownership of
the target. The collector is a different argv through original `Commands`, so
it cannot recursively trigger itself; original target stdout capture continues.
`/usr/bin/sample` gets one second, a five-second command deadline and an 8 MiB
output check. Triggering is recorded before sampling; sample interruption may
change the observed result and is recorded as such. Missing/failed collection
remains incomplete, even if the original prefix subsequently passes.

Local real-process checks use actual archive helper code: a target exits during
an independent collector command yet remains the owned, unreaped zombie through
the synchronous hook; quick targets are not sampled; collector failure preserves
the target's original exit 7 and stdout. Real compiled receipt fixtures exercise
original `run_suites`: success stops after persisted validated receipts; a failed
suite and missing required case propagate their original failures before any
partial-stop marker. These are controller checks, not native product gate proof.
The original-prefix Darwin run and actual native sampler execution remain
pending for one caller-reviewed control, followed by evidence adjudication.

## Action items

- [x] Preserve and read prior/current evidence without repeating the closed Linux probe.
- [x] Create isolated task branch/worktree from the exact base.
- [x] Add compile-time optional, bounded traces to actual watchdog, timer,
  dispatch, registration retirement and POSIX native close paths.
- [x] Prepare a branch-only manual Darwin diagnostic in the existing
  `v3-package-validation.yml`; preserve push/PR matrix commands and skip other
  jobs explicitly on manual dispatch.
- [x] Check enabled/disabled trace compilation, bounded output, errno preservation,
  ordinary-build argument elision, workflow syntax and changed-file lint.
- [x] Execute the first native diagnostic and retain actual installed-library provenance.
- [x] Prepare bounded in-memory capture with deferred output and explicit event timestamps.
- [x] Retain the completed runner-context buffered diagnostic result without cancellation.
- [x] Correct required privileged build/test execution and record/assert its context.
- [x] Retain the completed corrected privileged buffered diagnostic and actual root context.
- [x] Prepare one bounded unmodified-c0 original-package-prefix control.
- [ ] Execute that reviewed control once and adjudicate its original outcome/sample evidence.
- [ ] Establish cause from native evidence, then write and observe a failing
  regression before applying a narrow repair.
- [ ] Remove all temporary trace code/header and workflow edits before final merge.
- [ ] Run task-relevant final checks and Groundwork validation/finalization.

## Prior instrumented checkpoint scope and checks

`HTTPSERVER_DIAG_HEADER_DEADLINE` enables temporary typed metadata capture,
capped at 64 records per call site and 2,048 records overall. Each record stores
an explicit monotonic `event_ns`, a static format and copied scalar/pointer
values in fixed storage in the compiled library. Capture performs no formatting,
stderr writes, allocation or application mutex locking. The installed test and
library use that same buffer. Only after all four test fixtures have stopped and
the suite has completed does the test call flush to format and emit stderr.
Correlation uses connection IDs, operation pointers/sequences, socket descriptors
and registration generations; new notify/signal/ack metadata covers the observed
wake/retirement handoff. Time points use `duration_cast<nanoseconds>`; header
durations are explicit milliseconds. No request bytes, credentials or peer
content are retained. Capture and flush preserve errno; ordinary builds erase
trace argument evaluation, storage and diagnostic symbols. A discarded compile
branch retains format checking without executing stderr writes during capture.

Manual dispatch runs macOS only, preserving its selected compiler/runtime,
provider setup and artifact upload. It builds and installs TLS-off libraries at
`-O0 -g`, then uses existing `Commands`, `runtime_environment`, `compile_probe`,
dependency inspection, shared-linkage checks and suite receipt parsing to run
the existing four-case external HTTP1 integration suite against the installed
shared library built from the actual failing c0 selected archive. The macOS job
uses only `contents: read` and `actions: read` with the existing GitHub token to
download artifact `12508482722` from run `39969790346` attempt 1. Existing
`verify_download` validates artifact/run/repository/source binding and ZIP digest
`a08eb79186b13cf12ac8c639f424e13a7de1ee09df5547aab2b4200a951a5f05`.
The archive is exactly 3,053,890 bytes with SHA-256
`43124552f20bd00fceddc714856d605ca9caf017a849a991c34f16f437fcab83`.
Existing strict `extract_archive` extracts it without a checkout `.git` directory.
All original packaged file hashes must match the selected descriptor. Before
copying the seven traced private implementations and existing integration test,
each original hash must also match the c0 source snapshot. The private trace
header is the sole added file; every other source remains byte-identical to the
archive. Original/modified source manifests and per-file overlay hashes are
retained. The original archive is preserved separately; the overlaid source is
explicitly a diagnostic modified snapshot, not immutable release proof, and does
not set `V3_RELEASE_IDENTITY`. Build and test source paths/cwd use this extracted
tree. No sudo, build or test process receives the download token.

Compile/dependency hashes, commands, runtime selection and the failing suite log
are retained in `package-evidence/header-deadline-diagnostic`. This partial run is
explicitly diagnostic evidence and does not satisfy the package matrix gate.

Local checks passed: eight affected translation units with traces disabled and
enabled (`g++ -fsyntax-only`, format errors enabled), trace contract checks,
workflow YAML/Python parsing and shell syntax, and cpplint on all changed C++
files. The first syntax invocation lacked generated `config.h`; bootstrap plus
an out-of-tree TLS-off configure corrected that execution setup, and both
configured invocations passed. No full baseline or old Linux publication/wait
probe was run. The follow-up archive-wiring preflight uses the actual downloaded
selected artifact locally: exact ZIP/tar and original hashes pass, the literal
workflow extraction/overlay block passes, all eight original overlay files match
the exact base, and the private trace header is present. All eight affected
translation units also pass diagnostic-enabled syntax/format checks using only
the extracted source/include paths. Workflow syntax passes again; only workflow provenance and this task record change in the follow-up.
The buffered capture contract was tested RED/GREEN: live stderr output first
fails the deferred-output assertion; the new buffer passes. Enabled/disabled
checks link the actual io_operation implementation as a shared library and prove
no output before flush, 64/site caps across concurrent producers, errno
preservation, typed field formatting, positive per-event timestamps, one buffer
shared by executable/library events, and no diagnostic symbols or argument
evaluation in ordinary builds. The updated literal archive overlay and
workflow/test-preservation checks pass; both enabled/disabled extracted-source
syntax checks and changed-file lint pass. Buffered native execution and the
original required gate remain incomplete. For the privilege-only follow-up,
YAML, both Python stages and all macOS shell syntax pass; artifact/overlay code,
permissions, other jobs and all C++ files are unchanged. An actual local env
check proves both token variables are stripped while ordinary context survives;
the literal test launcher rejects local EUID 1000 before exec. A real tiny C
signal-report helper then demonstrated RED: direct Popen starts with available
SIGPIPE/SIGXFSZ defaults, while the prior Python exec fragment inherits ignored
dispositions. GREEN: the corrected literal signal/exec fragment yields identical
native defaults. SIGXFZ is checked when available. This proves the focused signal
correction locally, without claiming root execution; all native code and archive
bindings remain unchanged. These are setup checks; native sudo/build/test EUID 0
execution remains pending.

The caller reviews/publishes only this task branch and dispatches the existing
workflow, then resumes this same worktree with actual native evidence. No root
cause, behavior repair, validation completion or merge is claimed here. Tracing
can perturb scheduling, so a passing diagnostic alone would not prove a fix.

## Scope boundaries

The older Linux parity uncertainty remains closed. This task does not authorize
parity repairs, broad platform reruns, timeout changes, assertion relaxation,
dependency/policy changes or a new shared verification framework. Preserve the
unrelated bauth worktree. Nonlocal final platform checks remain assigned to CI
and the v3 PR under the repository working agreement.
