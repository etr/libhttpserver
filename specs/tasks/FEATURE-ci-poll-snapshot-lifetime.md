# FEATURE-ci-poll-snapshot-lifetime

Status: Complete — local validation/finalization complete; nonlocal CI proof pending, local LeakSanitizer unavailable/incomplete.
Base: local v3 `5a8f3e7532fff1b55ab6876096c6c9a76ac07a8b`.

Repair managed poll readiness isolation across release/readopt of a connection ID.
The original registration lifetime must accompany the captured projection and
remain authoritative at direction selection and later hangup teardown.

- [x] Demonstrate deterministic RED through a retained managed projection.
- [x] Reject stale readable/writable/hangup/error/invalid readiness.
- [x] Prove replacement during readable completion cannot authorize trailing teardown.
- [x] Prove descriptor close deferral and registration identity isolation.
- [x] Preserve current registration read/write/EOF/cancel/release/close behavior.
- [x] Clear concurrency doubts with durable evidence; run focused checks and lint.
- [x] Caller Groundwork validation sealed PASS session `0bb9a4790ebc936d95985e02eeced888`, all9 approve,1 iteration,0 fixed,0 unworked; local finalization/merge into v3.

Decision: retain each original registration's shared lifetime in the managed
poll projection, compare it under the registry mutex before selecting direction
work, and pass it to every later teardown. Socket syscalls and owner enqueue
remain outside the registry mutex. Existing detached batch leases and external
readiness contracts remain authoritative.

Doubts (open until focused evidence is recorded): an initial identity precheck
may race later teardown; ID plus numeric descriptor may alias a replacement;
stale read/write selection may consume new work; guards may suppress current
readiness or retain sockets indefinitely; identity captured at dispatch rather
than projection may bless stale flags. Refuting observations are replacement
completion/closure/data consumption under old flags, failed fresh readiness,
or a socket surviving final lease retirement.

Primary evidence: `.debug/ci-poll-snapshot-lifetime/journal.md` and its
`implementation/` directory. The supported stale managed projection defect is
compatible with native capture 40176129646 entry 14; the exact attempt 25
interleaving remains **unproven**. Capture is CLOSED after 25 comparisons and the
first mismatch; no further native execution is authorized. Nonlocal
BSD/Darwin/Windows checks belong to CI and the v3 PR. A normal revert is rollback
for this private repair. Caller owns validation, merge, and publication.

Implementation evidence (2026-10-25):

- Pre-fix RED: five deterministic owning tests, 673 checks, 351 expected
  failures (exit 95 = failure count modulo 256). Actual retained managed
  projections consumed replacement work, closed replacement registrations on
  trailing old HUP, and failed physical close deferral. Production source hashes
  and the original test patch are retained alongside the literal log.
- Focused pre-fix compatibility baseline: 12 tests / 127 checks passed.
- GREEN: 19 focused owning poll tests / 832 checks and eight affected external
  readiness contracts / 135 checks passed. Native core archive and configured
  owning test translation unit compiled. Four changed C++ files passed cpplint;
  `git diff --check` passed.
- ASan/UBSan: ten affected poll tests / 738 checks passed with the owning test TU,
  poll implementation, and external adapter instrumented; remaining dependencies
  used the normal TLS-off core archive. LeakSanitizer is **unavailable/incomplete**:
  its first run completed all assertions but exited 1 because this execution
  environment uses ptrace. The subsequent ASan/UBSan-only run explicitly disabled
  leak detection; it is not a passing LeakSanitizer result. No investigation or
  shared infrastructure repair was attempted.

All five concurrency doubts are cleared:

1. `poll_eof_completion_replacement_survives_old_hup` and
   `poll_data_before_eof_replacement_survives_old_hup` begin with a current
   snapshot, release/readopt synchronously during real owner completion, then
   execute its trailing HUP. New registration and both pending operations survive;
   fresh readiness transfers `new`. This fails with only the old unconditional
   teardown, so an initial precheck cannot satisfy the regression.
2. `poll_projection_lease_defers_close_until_snapshot_dropped` retains the actual
   projection, releases the ID, verifies the old weak lifetime remains alive and
   the descriptor remains valid, then verifies lifetime expiry and POSIX EBADF
   after dropping the projection. Numeric descriptor reuse while that captured
   snapshot exists is prevented by legitimate ownership. No descriptor was
   forcibly overwritten. The unchanged external
   `reopen_same_id_and_descriptor_rejects_every_old_flag_combination` also passes
   after legitimate release and exact numeric descriptor reuse.
3. `poll_retired_projection_cannot_touch_replacement` dispatches all 31 nonempty
   read/write/HUP/error/invalid combinations twice against an original retained
   projection. Replacement read/write remain pending, read buffer stays untouched,
   and peer observes no write. Fresh readiness then transfers exactly `new` and
   `out`, once each. `poll_completion_replacement_rejects_old_writable` covers
   replacement between readable completion and same-event writable selection.
4. Current HUP/error/invalid complete both pending operations once. Focused real
   read/write/partial read/EOF/accept/cancel/release/close/rearm contracts pass,
   including inline completion without a held registry mutex. Explicit lifetime
   expiry/EBADF provides descriptor retirement evidence independent of unavailable
   LeakSanitizer.
5. The test fixture stores the actual result of production `build_projection`
   and later invokes production `dispatch_revents` with it. It never resnapshots
   current identity at stale dispatch, and adds no production test API.

The only additional production callsite change is the private projection vector
in `io_external_adapter.cpp::retirement_progress`; its generation/identity API and
host dispatch behavior are unchanged. The first rebuild caught this required
private callsite update; failure evidence is retained. Existing generic friend
member-pointer warnings and bootstrap warnings are recorded in compile logs.
An early temporary selector include resolved the primary checkout; it was
corrected before any runtime baseline or RED and both compile receipts remain.
A wrong-working-directory callsite-edit attempt made no edit; its unsuccessful
rebuild receipt is retained. None is claimed as successful proof.

Native FreeBSD `external_readiness_adapter_test.cpp` time_point `++now` compile
failure is separately owned by queued FEATURE-ci-freebsd-chrono-fixture. This
GNU focused build passed; no native failure was repaired or claimed passed here.
Native BSD/Darwin/Windows validation remains CI/v3-PR owned. The exact historical
native interleaving remains UNPROVEN; capture CLOSED25/first mismatch is unchanged.
Implementation is ready for caller validation; merge/publication are not performed.
