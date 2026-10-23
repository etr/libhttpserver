# FEATURE-ci-darwin-header-deadline: Bounded native Darwin deadline diagnostic stage

Status: Complete — bounded diagnostic stage only. Groundwork validation passed
(1 iteration, 0 fixed, 0 unworked); the defect and full CI gates remain unresolved.

Base: `v3` at `c0cfdabc8040273d60c610269a93d1c687ed8517`.

## Goal and acceptance

Complete the user-approved bounded native investigation of the installed shared
TLS-off Darwin external header-deadline failure, preserve the evidence and its
limits, and remove every temporary implementation/test/workflow change. On
2026-10-23, after the final unmodified-source control passed, the diagnostic
stage was explicitly closed with an inconclusive causal outcome. This stage's
acceptance does not include or claim a defect repair or a passing package gate.
The original repair objective remains open in
[FEATURE-ci-darwin-header-deadline-followup.md](FEATURE-ci-darwin-header-deadline-followup.md).

Acceptance for this diagnostic stage:

- [x] Preserve original failing receipts and execute only the approved bounded native controls.
- [x] Record actual source, archive, installed linkage, compiler and execution context.
- [x] Adjudicate successful controls as intermittency, without claiming a cause, fix or independence.
- [x] Restore source, existing tests and workflows to exact base bytes; remove the temporary header.
- [x] Preserve unresolved assertions/budgets, evidence and limits in a separate pending task.
- [x] Verify documentation-only final delta and `git diff --check`.
- [x] Caller: Groundwork validation and serial task finalization.

## Original failure remains open

The installed shared TLS-off `external_http1_e2e` case
`published_header_deadline_drives_empty_dispatch` failed on f91 archive run
`39962644789`, Darwin job `120444991323`, artifact `12506406058`, and again on
exact c0 archive run `39969790346`, job `120469466623`, artifact `12509855171`.
On c0 it took 10,006.1 ms: assertion 217 (`receive_close`) and assertion 221
(`!failed`) failed; assertion 219 (`empty_dispatches > 0`) passed. Other three
cases and the preceding native HTTP1 suite passed. The 300 ms header budget,
10 s host failure deadline and all original assertions remain unchanged.

At the same c0 revision, package checkout job `120468733495` passed the TLS-off
lane and failed later in a separate TLS-on CA certificate check. Checkout versus
archive context was an observed contrast, never an established cause. Original
archive and checkout package execution used the existing root fallback after
port 443 binding failed. Earlier runner-context diagnostics did not match that
privilege context; later controls corrected it. The CA issue is separate.

## Completed bounded evidence

| Native run / macOS job | Control | Observed result |
| --- | --- | --- |
| `39974555288` / `120485650697` | Exact c0 archive plus live trace overlay, isolated external suite, runner context | Four tests / 52 checks pass; header 303.347 ms |
| `39978609885` / `120499691522` | Deferred bounded trace overlay, isolated external suite, runner context | Four / 52 pass; 677 records after statistics; healthy header near 303 ms |
| `39982282514` / `120512379909` | Deferred overlay, corrected root context and original signal defaults | Four / 52 pass; actual UID/EUID/GID/EGID 0; healthy timer/enforcement/native close near 303 ms |
| `39990090304` / `120539322802` | Unmodified exact c0 archive and original package prefix, direct original Popen, EUID 0 | Native HTTP1 60 / 708 and external HTTP1 four / 52 pass; header 303.348 ms |

The final control used driver commit
`60caf5d9ec6b9bd611ede1d1367a6ade168fe1e5`, actual source c0, original
`package_lane` prelude and the first two original TLS-off shared suite definitions.
Original suite-result, required-case, status and persisted receipt checks completed
before the explicit partial stop. Static linkage, remaining suites and TLS-on
were outside that control. It is not full package matrix gate passage.

Existing archive helpers verified selected artifact `12508482722` from run
`39969790346`, attempt 1. ZIP SHA-256:
`a08eb79186b13cf12ac8c639f424e13a7de1ee09df5547aab2b4200a951a5f05`.
The 3,053,890-byte archive SHA-256 is
`43124552f20bd00fceddc714856d605ca9caf017a849a991c34f16f437fcab83`.
The final receipt reports `source_unchanged: true`; source/helper bindings were
verified. Read-only artifact credentials stayed outside privileged build/test.

The driver could collect one owned-process sample after a three-second stall,
using the original non-reaping command ownership. Sampling was **not triggered**;
actual Darwin `/usr/bin/sample` collection remains untested. Local real-process
checks established controller ownership/failure propagation and stop-after-original-
receipt behavior, not native sampler execution or product gate passage.

Successful controls establish intermittency. No violated race invariant, failing
forced-interleaving regression or root cause was established. Instrumented passes
can reflect changed scheduling; the final unchanged-source pass also cannot
establish independence. Original failed gates remain open/incomplete. The bounded
investigation is finished; no further retry, probe or speculative repair is
part of this stage. A future diagnostic decision follows completion of the safe,
confirmed independent CI fixes, as recorded in the pending task.

## Cleanup and verification

All seven changed private implementation files, the existing external integration
test and existing `v3-package-validation.yml` were restored from exact c0 Git
blobs. The added `deadline_trace.hpp` was removed. Source, test and workflow trees
are exactly equal to base; the only persistent delta is this task documentation
and the pending follow-up card. Prior diagnostic commits and retained evidence
remain in history; no reset, rebase or amendment erased them.

Cleanup evidence is in the primary checkout's
`.debug/ci-darwin-native-69af4dxe/diagnostic-stage-cleanup.json` and
`diagnostic-stage-cleanup-check.log`. Byte/blob equality, absent trace identifiers
and the documentation-only diff were checked; `git diff --check` passes. With
no production/test/workflow delta, no C++ build or broad suite was rerun. Groundwork
validation passed at `31ccbeac65a37e85895163666281621fa726b51d` with three
approvals (1 iteration, 0 fixed, 0 unworked); caller finalization records the
diagnosis-only completion. Full CI-green acceptance remains incomplete.

## Evidence and boundaries

Primary journal: `.debug/ci-darwin-native-69af4dxe/journal.md`.
Final receipt summary/log: `original-prefix-summary.json`,
`original-prefix-external.log`; full diagnostic, sampling, suites and commands
receipts use the same `original-prefix-` prefix in that directory. Final evidence
artifact `12517856733` ZIP SHA-256:
`8580a5b6144ce5732aced8cfdee2f69ef413ca5e7d4bd85800bf783eba2da695`.
External stdout SHA-256:
`460335f8d3e88c2cd4211246cb6d29c141b51a5f19ec0071f17ea472c848ed7c`.
The original plan's diagnostic-stage adjudication is recorded in
`.groundwork-plans/FEATURE-ci-darwin-header-deadline-plan.md`.

The prior Linux publication/wait probe remains closed/uncertain in
`.debug/ci-darwin-header-deadline-ybz8bi7l/journal.md`; it was not repeated or
reopened. Windows staging, TLS CA fixture and FreeBSD selected-make corrections
are separate queued tasks. No unrelated worktree was changed, failed gate waived,
publication performed or overall CI-green objective declared complete.
