# FEATURE-ci-compiler-fixture-shell-isolation

Status: Complete (local validation/finalization; native CI pending).
Base: local v3 `284441346446e61489bea2d3bf2761b3e29a895a`.

Isolate the Windows inspection fixture's controlled shell selection from the
native compiler environment. The macOS package and archive runs each failed
four inherited inspection tests because their real `#!/usr/bin/env bash`
compiler wrapper selected the fixture's fake Bash from its prepended PATH.
Control only the owning runner's selected-shell lookup; preserve real compiler
and interpreter selection, isolated audit platform, full inspector evidence,
failed artifact identity, original assertions and competing-shell contracts.

- [x] Read the accepted plan, closed native evidence and owning subprocess boundaries.
- [x] Create the Groundwork worktree from the exact v3 baseline and verify affected baseline tests.
- [x] Demonstrate RED with a real Bash compiler wrapper delegating the selected host C++ compiler.
- [x] Isolate selected shell lookup without changing the native child environment.
- [x] Verify focused inspection/shell contracts, Python syntax and diff whitespace.
- [x] Caller validation and finalization.

Primary evidence: `.debug/ci-284-macos-compiler-shell-cgzzte1a/journal.md` and
`implementation/` beside it. Local baseline: 14 affected inspection/shell tests
passed. Actual macOS package `120844695418` and archive Darwin `120844856150`
failures remain recorded; post-fix nonlocal checks remain CI/v3-PR owned and
incomplete. Controlled producer output is not a native Windows/Darwin receipt.

The new regression failed before inspection because the actual env-shebang
compiler selected `selected tools/bash` and could not execute `controlled`.
After the fixture-only repair, the same regression passes through real object
compilation and linking, retains a link map and verifies the failed artifact's
SHA and successful compile/link command receipts. Native Windows explicitly
invokes the real Bash interpreter for this script; no test skip was added.

Local verification: 16 focused inspection, selected-shell, real `ps`, native
compile-host and path-conversion receipt tests pass. The existing direct early
receipt recovery test also passes through both package/archive EXIT copyback
and real SCP peers. Python byte-compilation and diff whitespace checks pass.
No Python linter is documented; cpplint applies to unchanged C++. All four
original parent inspection methods and both competing-shell classes retain
identical ASTs; production, native wrapper and recovery inputs retain identical
bytes (durable `implementation/preserved-contracts.json`).

The two pre-edit InstalledMakeTest configure timestamp failures remain
FAILED/INCOMPLETE with uncertain causality, closed and unretried. Epoll's
64-case budget is exhausted and adapter evidence remains CLOSED/UNKNOWN.
No broad baseline, native library build, unrelated repair, CI control or
publication is authorized by this task.

Groundwork validation `e5fdee786c9737d95d46a501d2dc8c69` sealed PASS for
`71eb5155482d8521ede839c32292353113e3f297` / tree
`14d88a6d38461bbed70a5b0615314639a3d4d3d7`: one iteration, nine fresh
independent approvals, zero fixes and zero unworked findings. Source-bound
passing evidence reused; no report created. Native FreeBSD package/archive
also reproduced the same four fixture errors on 284; their gates remain failed
and post-fix platform checks remain incomplete, CI/v3 PR owned. Original closed
uncertainties remain preserved. Root owns combined publication and next CI.
