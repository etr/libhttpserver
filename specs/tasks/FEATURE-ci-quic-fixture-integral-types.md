# FEATURE-ci-quic-fixture-integral-types: portable QUIC fixture max operands

Status: Implemented; scoped local and native Darwin compile checks pass. Caller validation and finalization pending.
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
- [x] Execute the user-authorized single native mac compile-only proof: original
  owner RED once, then both repaired owning TUs GREEN, with actual CI
  OpenSSL 3.5.9 headers and native configuration retained from the verified ZIP.
- [x] Caller Groundwork validation and local finalization.
- [ ] Post-fix exact Darwin/FreeBSD CI proof (CI/v3 PR-owned; incomplete and
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

The separately authorized native compile-only proof uses `ssh mac`, macOS
26.3.1 arm64, Homebrew clang/libc++ 22.1.8 and macOS SDK 26.5. The original
54fa owner TU reproduces the exact line267 `unsigned long` / `unsigned long
long` deduction failure once. Both repaired actual TUs from `cafb8055` then
pass `-fsyntax-only` without warnings; no source expression changed further.
This host differs from failing archive CI clang23/SDK15, so this proves native
Darwin source compatibility in the recorded environment, not an exact CI pass.

Only 142 provider headers (about 2 MB) and the native TLS-on `config.h` were
extracted from the verified existing macOS package-evidence ZIP, SHA256
`471035241523fe7baf88f737385af6d674a94d78a6629a52103f834089a165de`.
They are the actual CI OpenSSL 3.5.9 headers/configuration; Homebrew OpenSSL
3.6.3 was not substituted. The source/header snapshot plus original owner
was 3.8 MB transferred to the exclusively owned remote subdirectory
`/tmp/libhttpserver-v3-repro.dxubki/compile-fixture-types` (canonical
`/private/tmp/...`). No Linux-generated configuration was used remotely.
Exact argv, monotonic durations, native compiler/SDK/header/source hashes,
raw results and canonical paths are retained in `implementation/native-mac/`.
No executable was linked or run and no provider/core/SDK rebuild occurred.
Native FreeBSD postfix proof and the complete CI gates remain incomplete.

No test program or network probe was executed. No unrelated source/expression
search, repair, broad suite, publication or diagnostic dispatch belongs here.
Windows capture16 remains pending, zero executed. Closed kqueue25, exhausted
epoll64, header/outbox/clock and Windows WSAPoll/post-summary unknowns are
preserved. The new Mac HTTP2 startup deadline remains unassessed and is not
attributed to preflight EACCES. Delayed Darwin five-plus-header approvals refer
to the already closed 40055653509 batch, with all six executed once; no new
native diagnostic budget is inferred.

Full Groundwork session `c653da50d15acba788d357185b3ad9e8` sealed PASS:
one iteration, nine approvals, zero findings/fixes/unworked. All retained gate
inputs/log hashes match; no unnecessary reruns. Native Mac original-owner RED
and both repaired-TU GREEN remain qualified by the recorded host/CI compiler
and SDK differences. No test program or broad suite ran.
