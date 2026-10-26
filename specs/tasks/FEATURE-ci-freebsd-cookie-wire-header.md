# FEATURE-ci-freebsd-cookie-wire-header: declare the POSIX cookie fixture's IPv4 socket type

Status: Implemented; focused local checks pass. Caller Groundwork validation and
finalization pending; native FreeBSD post-fix proof remains incomplete.
Base: exact local `v3` at `16da2678cce4f1b333c85b178514789f168078db`.
Plan: `.groundwork-plans/FEATURE-ci-freebsd-cookie-wire-header-plan.md` in the primary checkout.

## Goal and scope

Include `<netinet/in.h>` directly in the existing non-Windows include branch of
`test/integ/consumer_v3_cookie_wire.cpp`. Actual FreeBSD CI reports seven errors
because `sockaddr_in` and its fields are unavailable from the previous headers.
The official FreeBSD declaring header was verified in the prior peer-policy
repair. Preserve every other source byte, including the Windows branch, cookie
and CRLF injection assertions, socket calls, modes and deadlines.

## Action items

- [x] Read the accepted plan, closed journal and retained project memory; use
  the resolved isolated Groundwork worktree from the exact local v3 base.
- [x] Retain original source and actual native compiler RED with exact hashes
  before editing; reuse the closed corroborated assessment.
- [x] Add only the direct POSIX declaring header; add no mirrored test/helper.
- [x] Compile the actual owning TU to a GNU object and check its syntax using
  existing real LLVM/libc++; preserve exact flag/include provenance.
- [x] Pass changed-file cpplint, diff and exact byte-contract checks; retain
  command/output, source, tool, configuration and dependency hashes.
- [ ] Caller Groundwork validation and local finalization.
- [ ] Native FreeBSD post-fix compilation/execution (CI/v3 PR-owned; incomplete,
  not a local completion or merge blocker under project policy).

## Verification and limits

Journal: `.debug/ci-freebsd-cookie-wire-header-6tq8y6at/journal.md`.
Evidence: `.debug/ci-freebsd-cookie-wire-header-6tq8y6at/implementation/`.
Native RED: archive run `40245179944`, FreeBSD job `121326662156`, actual
`gmake check -j2`, seven `sockaddr_in` and dependent member errors. Original
source SHA256 `3d7338ac3f9b9f36c7178b3c526aa754dfba721e6e7a243a52a00911b69a518e`;
raw log SHA256 `7eb37856f2d5798d037aeb76c4d02c18d81e76d173a6b846ef422cd6d394b0e5`.

GNU 13.3.0 object compilation and LLVM 19.1.1 with real libc++ 20.1 syntax
checking pass with empty diagnostics. Flags combine cached generated
`build/test/Makefile` common flags with the current owning target's
`Makefile.am` CPPFLAGS (`src`, `test`, `examples`). The cached generated
Makefile predates this target; no exact generated cookie target rule is
claimed. Both dependency traces include the actual `examples/host_poll.hpp`
and system `netinet/in.h`; neither reads `config.h` or private provider
headers. Exact byte comparison confirms the single include insertion is the
entire fixture delta and the Windows branch is identical.

No bootstrap, provider/core/SDK build, consumer runtime, network probe, full
suite, baseline retry, new native replay, VM, CI action or push was performed.
Local Linux compiler checks do not establish native FreeBSD post-fix success.
Prior closed diagnostic budgets and unrelated UNKNOWN assessments remain
unchanged. Caller owns validation, merge, cleanup and publication.
