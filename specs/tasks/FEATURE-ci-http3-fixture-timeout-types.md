# FEATURE-ci-http3-fixture-timeout-types: portable HTTP3 fixture timeout max

Status: Complete locally; scoped compiler checks and Groundwork validation pass. Native FreeBSD CI proof remains incomplete.
Base: exact local `v3` at `7d5e212acddf8b09dbbc9d5e2602c6a4d5b825f1`.
Plan: `.groundwork-plans/FEATURE-ci-http3-fixture-timeout-types-plan.md` in the primary checkout.

## Goal and scope

Repair the HTTP3 UDP fixture's timeout `std::max` deduction failure when
`std::int64_t` is `long` and `std::chrono::milliseconds::rep` is `long long`.
Specify only `std::max<std::int64_t>`, matching the existing zero operand.
Preserve ceil milliseconds, the nonnegative lower bound, assignment to `int`,
deadlines, readiness processing and the `poll` invocation. No production or
runtime behavior change is introduced.

## Action items

- [x] Read the accepted plan, literal primary journal and executor memory;
  create a fresh Groundwork worktree from the exact local v3 base.
- [x] Before editing, compile the actual original owning TU with existing
  real LLVM19/libc++20 and confirm the conflicting max deduction error.
- [x] Add only the explicit `std::int64_t` max template argument; verify all
  other owning-source bytes match the base.
- [x] Compile the repaired actual TU to a GNU object and syntax-check it with
  real LLVM19/libc++20 using the existing configuration/provider headers.
- [x] Run changed-file cpplint and `git diff --check`; retain actual commands,
  raw outputs and source/tool/configuration/provider/dependency hashes.
- [x] Caller Groundwork validation and local finalization.
- [ ] Native FreeBSD post-fix CI proof (CI/v3 PR-owned; incomplete and not a
  local completion or merge gate).

## Verification and limits

Primary journal: `.debug/ci-http3-fixture-timeout-types-j48bo64i/journal.md`.
Evidence: `.debug/ci-http3-fixture-timeout-types-j48bo64i/implementation/`.

Original actual TU SHA256:
`57ca4df21734501b4948a73daffc3f7fcee4a0dc4b256851131dbb57e2a1cbbb`.
Real LLVM19.1.1/libc++20 rejects line387 with conflicting `long` / `long long`
deduction before any edit. The initial command also omitted this fixture's
`PARITY_DATA_ROOT` define; its failure evidence remains preserved. After
restoring that existing Makefile flag, the unchanged TU fails with exactly the
expected max error. This is local compiler proof, not native FreeBSD replay.

The repaired TU passes GNU13.3 object compilation and real LLVM19.1.1/libc++20
syntax checking; both produce empty diagnostics. Changed-file cpplint and diff
checks pass. A byte comparison proves the explicit template argument is the
only owning-source change. No mirror typedef test, runtime program, network
probe, broad suite, bootstrap, provider/core rebuild or unrelated repair ran.
Source/input hashes and exact argv are in `red-commands.json`,
`green-commands.json`, their dependency manifests and `checks.json`.

Native FreeBSD post-fix execution remains incomplete and CI/v3 PR-owned.
Earlier closed diagnostic budgets and UNKNOWN causal assessments remain
unchanged. Groundwork session `15f446458d448bd80f216993805eaac7` sealed
PASS: all nine reviewers approve, one iteration, zero fixes and zero unworked
findings. Source bytes are unchanged from the validated implementation.
Caller performs the normal local merge, publication and exact worktree cleanup.
