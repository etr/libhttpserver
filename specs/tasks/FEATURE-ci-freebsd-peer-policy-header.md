# FEATURE-ci-freebsd-peer-policy-header: declare the POSIX peer-policy fixture's IPv4 socket type

Status: Complete locally; scoped compiler checks and Groundwork validation pass. Native FreeBSD post-fix proof remains incomplete.
Base: exact local `v3` at `efd155b70b80004969b7bb3f929712b5100b1c30`.
Plan: `.groundwork-plans/FEATURE-ci-freebsd-peer-policy-header-plan.md` in the primary checkout.

## Goal and scope

Include `<netinet/in.h>` directly in the existing non-Windows include branch of
`test/integ/consumer_v3_peer_policy_runtime.cpp`. The actual FreeBSD compiler
cannot see `sockaddr_in` from the fixture's previous headers. This header owns
that declaration. Preserve every other source byte, including the Windows
branch, peer-policy assertions, modes, socket arguments and timeouts.

## Action items

- [x] Read the accepted plan, literal journal and project memory; create the
  resolved isolated Groundwork worktree from the exact local v3 base.
- [x] Retain the actual native compiler RED and original source hash before
  editing; use this existing RED for the reversible fixture include repair.
- [x] Add only the direct POSIX declaring header; add no mirrored test/helper.
- [x] Compile the actual owning TU to a GNU object and syntax-check it using
  existing real LLVM/libc++ with the actual target flags and examples include.
- [x] Run changed-file cpplint, diff and exact byte-contract checks; preserve
  commands, outputs and source/tool/configuration/dependency hashes.
- [x] Caller Groundwork validation and local finalization.
- [ ] Native FreeBSD post-fix compile/execution (CI/v3 PR-owned; incomplete,
  not a local completion or merge blocker under project policy).

## Verification and limits

Primary journal: `.debug/ci-freebsd-peer-policy-header-3pre56r6/journal.md`.
Evidence: `.debug/ci-freebsd-peer-policy-header-3pre56r6/implementation/`.
Original native RED: archive run `40237011225`, FreeBSD job `121300744644`,
`gmake check -j2` TLS-off, seven compiler errors for `sockaddr_in` and its
fields. The original fixture is byte-identical at the accepted base and
`37095c23751960b6e1adcad95d7b3c4c34c64351`; original source and raw log hashes
are retained. The official FreeBSD `sys/netinet/in.h` declares `sockaddr_in`.

GNU 13.3.0 object compilation and LLVM 19.1.1 with real libc++ 20.1 syntax
checking pass with empty diagnostics. Flags follow the owning generated
`build/test/Makefile` target, including the actual `examples/host_poll.hpp`
include path. Existing configuration is hashed; this TU does not require
private provider headers. Both compilers' dependency lists and hashes are
retained. Changed-file cpplint and diff checks pass. An exact byte contract
proves the single include insertion is the entire fixture delta and preserves
the Windows branch. Local Linux compilation is not a native FreeBSD rerun.

No bootstrap, provider/core/SDK build, consumer runtime, network probe, full
suite, baseline rerun, native execution, CI API action or dispatch was run.
Native post-fix proof remains incomplete and CI/v3 PR-owned. All earlier
closed diagnostic budgets and unrelated UNKNOWN assessments remain unchanged;
the remaining fresh native capture proposal is neither requested nor approved.
Caller owns validation, merge, cleanup and publication.

Groundwork session `30d23d4f87dbfbd3b107ab5ae6fb7811` sealed PASS: nine
approvals, one iteration, zero fixes and zero unworked findings. Source is
byte-identical to validated `eb61f261a2f1d377eb3dc7c4fbb5d8dd7fade106`.
Caller performs the normal local merge, publication and exact worktree cleanup;
all native post-fix checks above remain incomplete and CI/v3 PR-owned.
