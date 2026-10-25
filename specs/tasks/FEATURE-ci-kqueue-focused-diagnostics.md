# FEATURE-ci-kqueue-focused-diagnostics

Status: In Progress — diagnostic implementation ready for caller validation; native capture pending, **0/64** native kqueue attempts/comparisons executed.

Base: `v3` at `a9c800522afe6d2abea308e0be9215fda1b2826e`.
Diagnostic branch: `task/FEATURE-ci-kqueue-focused-diagnostics`.
**NEVER merge or publish this diagnostic into v3.** Caller owns validation, diagnostic branch push, one manual dispatch, batch observation and disposition.

User approval: `Run bounded native capture`, recorded in the primary journal. This is one new Darwin kqueue batch; it does not reset the exhausted local epoll batch or reopen the closed adapter/header/outbox/clock investigations.

Question: which original poll/kqueue normalized trace entry differed in the actual 47c Darwin TLS-off installed-static failure at `io_kqueue_backend_test.cpp:603`? Original vectors were absent. Cause remains **UNKNOWN**.

- [x] Preserve `semantic_trace` byte-for-byte, all 48 registered cases, every original check, framework startup and original full-program path.
- [x] Emit complete byte-safe JSON vectors and every differing index only after both original collections; select the original trace case directly for later processes.
- [x] Implement one durable, nonresumable, single-flight driver: debit before launch; full 48-case component once, then focused case only; cap 64 total attempts including the first; stop first nonzero/mismatch/invalid receipt/crash/timeout. Bound each process to 20 seconds and execution batch to 300 seconds.
- [x] Preserve raw stdout/stderr, hashes, native exit/signal statuses, comparison tokens and original summary evidence. A zero child exit with differing vectors fails the driver. All 64 passing closes only as NONREPRODUCTION/UNKNOWN, never release acceptance.
- [x] Reuse the retained supported manual-dispatch `release.yml` pattern as reference only: sentinel input, exact branch, attempt 1, read-only permissions, no tag/push/release/deploy trigger. Normal `v3-release-acceptance.yml` unchanged.
- [x] Restrict native setup to unchanged compiler/runtime selector, pinned provider, TLS-off installed library/headers and original owning translation unit. Match original `-O0 -g`, EUID 0, explicit static archive and original compile/link arguments; record current artifact identities and provenance differences honestly. Existing selector cancellation sanity runs once as required setup, separately from kqueue comparisons. No LLVM libc++ DYLD override or debugger.
- [x] Offline TDD with Python fake children only: 16 tests cover full-first/focused-rest/cap, first/focused failures, mismatch, invalid/incomplete receipt, timeout/crash/launch failure, durable debit, reentry and live concurrent refusal. Syntax, source preservation, changed-file cpplint and actionlint checked locally.
- [ ] Caller full Groundwork validation and review before any outward diagnostic push/dispatch.
- [ ] One native Darwin batch and honest terminal accounting, within the original approved 64-attempt cap. No Linux C++ test execution or native comparison has occurred during implementation.

Primary evidence: `.debug/ci-47c-darwin-suite-receipt-3sqav3tc/journal.md`; implementation receipts under its sibling `diagnostic-implementation/`. Original commands and both original suite JSON receipts remain retained there. Plan: `.groundwork-plans/FEATURE-ci-kqueue-focused-diagnostics-plan.md` in the main checkout.

Native compile, loader behavior and actual comparison outcome remain unverified locally. No native gate is represented as passed. No source/runtime repair is authorized by this diagnostic task. Any unrelated original assertion failure stops the batch with cause unknown; it authorizes no follow-up probe or subsystem repair.
