# FEATURE-ci-temp-parent-identity: physical native temporary-parent checks

Status: Complete (local). Scoped gates and full Groundwork validation passed; native Windows verification remains CI-owned.
Base: exact local `v3` at `0246d7a27774866ba96bd60c7d0fed553f6dee4d`.
Plan: `.groundwork-plans/FEATURE-ci-temp-parent-identity-plan.md` in the primary checkout.

## Goal and scope

Repair the two native temporary-fixture parent identity checks that throw
`ENOENT` on Windows when the native temporary directory ends with a separator.
Normalize only the terminal empty directory component, preserving root paths,
then retain throwing `std::filesystem::equivalent` physical identity checks.
Preserve fixture creation, binary contents, uniqueness and cleanup assertions.

## Action items

- [x] Read the saved plan, literal existing journal, project agreement and executor
  memory; create the isolated worktree from the exact local base.
- [x] Bind RED to actual Windows archive 40186948552/job 121149938926: only
  the two new fixture checks throw for the same directory with/without its
  trailing separator. Original one-shot and multipart HTTP cases pass.
- [x] Normalize the two check endpoints root-safely; retain physical identity,
  alias behavior and filesystem exceptions without fallback or skip.
- [x] Preserve all existing uniqueness/content/binary/cleanup assertions and
  verify wrong-parent rejection and root preservation if adding a local helper.
- [x] Run only the two existing fixture cases under process-local TMP/TEMP/TMPDIR
  with spaces, with and without trailing separator; verify cleanup.
- [x] Verify GNU owning-TU compile/link, LLVM/libc++20 owning-TU syntax,
  changed-file cpplint and `git diff --check`.
- [x] Retain commands, output, source hashes and causal limits in the primary
  journal's `identity-implementation/` directory; commit only owned files.

## Verification and limits

Primary journal: `.debug/ci-251-windows-budget-receipt/journal.md`.
Evidence: `.debug/ci-251-windows-budget-receipt/identity-implementation/`.

The retained core archive is 5,920,268 bytes with SHA256
`96ec5c852ff311bc2c1053098f11580670f1f76c90bcacba42a74805eff9c820`.
`src/`, `include/` and `configure.ac` match its recorded source head
`87f73b3cd7116ea61c7bb423e770cc1c1bc34db6`; the root Makefile delta only
adds the Python SDK metadata module to EXTRA_DIST. No core/provider/SDK rebuild.

Native Windows post-fix execution and other nonlocal checks remain CI/v3-PR
owned and incomplete. Windows post-summary 180-second command budget remains
CLOSED/UNKNOWN; distinct WSAPoll and external-peer-close issues remain closed
unknown. Windows capture16 proposal is pending and was not executed. Closed
kqueue25/epoll64/header/clock/adapter investigations stay closed. No full native
program, full baseline, native process probe, timeout repair, publication,
merge or cleanup belongs to this implementation.

Local pre-change two-fixture baseline: 24 checks pass on Linux. This does not
replace the native Windows exception RED. Post-change only those same two
fixture cases ran in four environment forms: physical parent with/without
trailing separator and symlink parent with/without trailing separator. Each
process passed 26 checks, zero failures/skips, and left its controlled parent
empty. The two additional checks cover native root preservation and physical
rejection of distinct scratch directories after separator normalization.

GNU compiles the full owning TU via the task-local selector and links it with
the actual parity parser and verified unchanged core. Existing LLVM/libc++20
compiles the actual owning TU with `-fsyntax-only`. Both compile gates,
changed-file cpplint and diff checks pass. No production code, fixture helper,
original HTTP case or other test was changed or executed. The selector never
calls the original full-suite main.

One source-compatibility precondition initially reported a root Makefile delta;
inspection showed only EXTRA_DIST adds `scripts/lib/compiler_sdk.py`.
The corrected core-input comparison (`src/`, `include/`, `configure.ac`) is
empty. This is a recorded setup assessment, not an ignored failing gate.
Parallel compilation temporarily left a compiler assembly file in the shared
controlled parent at lint/syntax receipt time; all four sequential runtime
receipts record an empty parent after exit.

## Caller finalization

- [x] Full Groundwork validation sealed PASS in session
  `a694e258a832a1abe3fc195bc2f8f055`: one iteration, all nine profiles
  approve, zero findings/fixes/unworked. Retained gates remain input-bound.
- [x] Local task completion and finalization.
- [ ] Post-fix native Windows CI/v3 PR proof (incomplete; not a local merge gate).

No production/runtime repair or native diagnostic was added. The command budget
and distinct external-consumer WSAPoll issue remain UNKNOWN; the separate
Windows16 capture remains pending authorization.
