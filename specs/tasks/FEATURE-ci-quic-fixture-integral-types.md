# FEATURE-ci-quic-fixture-integral-types: portable QUIC fixture max operands

Status: Implemented; scoped local checks pass. Caller validation and finalization pending.
Base: exact local `v3` at `54fa9dd32572d7da7f4921a5735a4035908e96f0`.
Plan: `.groundwork-plans/FEATURE-ci-quic-fixture-integral-types-plan.md` in the primary checkout.

## Goal and scope

Repair the two source-proved fixture `std::max` deduction failures: Darwin's
`size_type` versus `uint64_t` in CRYPTO reconstruction and FreeBSD's `int64_t`
versus `chrono::milliseconds::rep` in the interop poll timeout. Supply only
explicit template types: `std::uint64_t` for the existing CRYPTO offset/end and
`std::int64_t` matching the timeout's existing outer `std::min`.

## Action items

- [x] Read the accepted plan, literal primary journal and executor memory; use a
  fresh isolated Groundwork worktree from the exact local base.
- [x] Verify RED against actual current native compiler logs: Darwin archive
  40196418221/job 121178275259 and FreeBSD package 40196409784/job 121178075715.
- [x] Change only the two `std::max` template arguments. Preserve byte arithmetic,
  resize, copies, TLS assembly/checks and the existing ceil-millisecond timeout
  with lower zero / upper 1000 clamp and unchanged poll invocation.
- [x] Compile both actual owning translation units with GNU; syntax-check both
  with existing real LLVM/libc++20 and private OpenSSL 3.5.9 provider headers.
- [x] Run changed-file cpplint and `git diff --check`; retain exact commands,
  raw outputs and source/input hashes in the primary journal's `implementation/`.
- [ ] Caller Groundwork validation and local finalization.
- [ ] Post-fix native Darwin/FreeBSD CI proof (CI/v3 PR-owned; incomplete and
  not a local completion or merge gate).

## Verification and limits

Primary journal: `.debug/ci-quic-fixture-integral-types-b4ykc2x2/journal.md`.
Evidence: `.debug/ci-quic-fixture-integral-types-b4ykc2x2/implementation/`.

The actual native CI compiler errors provide RED without a mirror typedef test.
Local pre-change GNU endpoint compile passes; real LLVM/libc++20 rejects the
actual endpoint line with the same conflicting `long` / `long long` types.
The corrected owner baseline compiles in both local toolchains; this is not a
Darwin reproduction. The initial owner commands lacked the normal test include
path and failed to find `littletest.hpp`; corrected task-local include setup was
retained and passed before editing. Initial `-Wextra` also emitted existing
fixture initializer warnings; final commands use the repository's `-Wall`.

Post-change GNU compiles both owning TUs to objects, and LLVM 19.1.1 using real
libc++20.1.8 headers syntax-checks both. All four return zero without warnings;
cpplint and diff checks pass. Existing provider headers/configuration and build
configuration were reused; no provider, core, SDK or bootstrap rebuild occurred.
Linux compile results do not establish native Darwin/FreeBSD execution.

No test program or network probe was executed. No unrelated source/expression
search, repair, broad suite, publication or diagnostic dispatch belongs here.
Windows capture16 remains pending, zero executed. Closed kqueue25, exhausted
epoll64, header/outbox/clock and Windows WSAPoll/post-summary unknowns are
preserved. The new Mac HTTP2 startup deadline remains unassessed and is not
attributed to preflight EACCES. Delayed Darwin five-plus-header approvals refer
to the already closed 40055653509 batch, with all six executed once; no new
native diagnostic budget is inferred.
