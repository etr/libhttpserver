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
- [ ] Execute one reviewed buffered-trace native diagnostic against the same exact archive.
- [ ] Establish cause from native evidence, then write and observe a failing
  regression before applying a narrow repair.
- [ ] Remove all temporary trace code/header and workflow edits before final merge.
- [ ] Run task-relevant final checks and Groundwork validation/finalization.

## Checkpoint scope and checks

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
tree. No build command receives the download token.

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
original required gate remain incomplete.

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
