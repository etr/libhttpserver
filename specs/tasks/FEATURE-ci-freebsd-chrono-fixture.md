# FEATURE-ci-freebsd-chrono-fixture

Status: Complete — local validation/finalization complete; native FreeBSD post-fix proof CI-owned/incomplete.
Base: local v3 `19d80795dfcfae17b10ea542bd3254376bfc2343`.

Repair the FreeBSD C++20 compile failure in the existing external readiness
timer cancellation/expiry race fixture. Advance its host time by one native
steady-clock tick using `now += clock_type::duration{1};`.

- [x] Verify retained native FreeBSD RED diagnostic and unchanged owning source.
- [x] Replace only the unsupported `++now` fixture expression.
- [x] Preserve 32 race iterations, barriers, cancellation, allowed outcomes,
  exactly-once delivery assertions, and final zero pending count.
- [x] Compile the actual owning translation unit with GNU C++20 and check its
  syntax with the existing Linux LLVM/libc++20 SDK.
- [x] Run only `timer_cancel_expiry_race_is_exactly_once` through the existing
  littletest selector pattern against the hash-verified retained GNU core.
- [x] Pass changed-file cpplint and `git diff --check`.
- [x] Caller Groundwork validation sealed PASS `9892e8837fafee1b9baf1b44cbd51979`, all9 approve,1 iteration,0 fixed,0 unworked; local finalization/merge into v3.
- [ ] Native FreeBSD post-fix compile/runtime proof: owned by CI and the v3 PR;
  incomplete here and not a local completion blocker under project policy.

Evidence: `.debug/ci-47c-freebsd-chrono-increment/journal.md` and its
`implementation/` directory in the primary checkout. The real FreeBSD job
`121099184796` from package run `40170157075` rejects the original owning TU at
line 519 before any test body executes. Its source SHA256 matches the exact
local base fixture. This retained failure supplies RED; no mirrored test was
added for the reversible expression change.

Local GREEN: GNU owning TU compile, LLVM 19 compiler with real libc++ 20.1.8
headers syntax check, focused GNU link, one race test / 97 checks passed.
The GNU core archive SHA256 is
`96ec5c852ff311bc2c1053098f11580670f1f76c90bcacba42a74805eff9c820`;
its production/build inputs are identical between retained source `87f73b3c`
and this task's base. No core rebuild, SDK installation, other adapter runtime
case, full baseline, or foreign program execution was performed. Existing
friend declaration warnings remain recorded; cpplint reports no errors.

No production/API/ABI/toolchain policy changes. The earlier configure-clock
failures and private-archive assessment remain separate and closed to further
investigation. Native kqueue capture CLOSED25 and epoll capture CLOSED64 are
unchanged. Caller owns validation, merge, cleanup, and combined publication.
