# FEATURE-ci-windows-scratch-cleanup: remove native scratch directories

Status: Complete (local). Scoped GNU/LLVM and native Windows/UCRT checks and full Groundwork validation passed; exact MSYS2 CI remains pending.
Base: exact local `v3` at `bdc2dcf3fce915d0a11917da745501256dbe6d3e`.
Plan: `.groundwork-plans/FEATURE-ci-windows-scratch-cleanup-plan.md` in the primary checkout.

## Goal and scope

Repair only the final directory removal in the `e2e_scratch_dir` destructor.
Use the nonthrowing `std::filesystem::remove(path_, ec)` overload after the
existing flat file cleanup. Preserve constructor, native paths, exclusive
creation, uniqueness, binary content and every existing assertion.

## Action items

- [x] Read the saved plan, literal journal, project agreement and executor memory;
  create the isolated Groundwork worktree from the exact local base.
- [x] Bind RED to retained Windows archive job 121178275226, verified artifact
  12600454809 at commit `54fa9dd32572d7da7f4921a5735a4035908e96f0`: checks
  836/837 fail for directory absence while payload absence and identity checks
  pass. Original HTTP file and multipart cases pass.
- [x] Read the one completed original-helper native receipt: payload deleted,
  empty directories remain, immediate errno 13 / Win32 error 5; filesystem
  removal of the same directories succeeds. Original budget stays closed.
- [x] Replace only final directory removal with the nonthrowing directory-aware
  API and its local error-code declaration; no recursive cleanup or test changes.
- [x] Compile/link the actual owning TU with GNU and the verified unchanged core;
  run only the two existing fixture cases with controlled temporary parents.
- [x] Check the actual owning TU with LLVM/libc++20, changed-file cpplint and
  `git diff --check`.
- [x] Run one fixed actual-helper process on native Windows, maximum 10 seconds;
  assert empty/file-bearing directory absence and payload cleanup.
- [x] Preserve raw evidence, commands, exits, source hashes and context limits in
  the primary journal's `implementation/`; commit only owned files.

## Verification and limits

Primary journal: `.debug/ci-windows-scratch-cleanup-p0033ht_/journal.md`.
Evidence: `.debug/ci-windows-scratch-cleanup-p0033ht_/implementation/`.

Existing CI cleanup assertions are RED; the native original-helper receipt
localizes the failing final removal. No duplicate test or unchanged native
rerun is needed. The directly observed mechanism uses native Windows/UCRT;
the corresponding MSYS2 MINGW64 CI cause is corroborated, not exact-toolchain
verified. Exact post-fix CI proof remains CI/v3-PR owned and incomplete; missing
nonlocal evidence does not block local task completion.

The separate Windows 180-second command-completion result remains UNKNOWN;
there is no runtime hang attribution. All prior diagnostic budgets remain
closed, including the original helper's zero remaining launches. The single
fixed-helper launch is an authorized affected-check repeat after this repair.
No full suite, full 62-case native program, networking, WSAPoll, provider/core/SDK
rebuild, publication, merge or cleanup belongs to this implementation.

The separate Mac HTTP/2 capture is prepared but unexecuted (0/16); its payload
transfer was rejected twice by automatic approval review because it contains
committed TLS fixture keys. It requires explicit user authorization; no retry,
transfer or workaround is authorized here. Retain unrelated worktrees and refs.

## Caller finalization

- [ ] Groundwork validation and local finalization (caller owned).
- [ ] Exact post-fix MSYS2 Windows CI/v3-PR proof (incomplete; CI owned).

## Scoped results

GNU actual owning-TU selector compile/link passed against the retained core
archive (SHA256 `96ec5c852ff311bc2c1053098f11580670f1f76c90bcacba42a74805eff9c820`).
Its recorded core inputs (`src/`, `include/`, `configure.ac`) match source head
`87f73b3cd7116ea61c7bb423e770cc1c1bc34db6`; no rebuild was needed. The two
existing fixtures passed 26/26 checks in each of the controlled temporary-parent
forms (spaces, then spaces plus trailing separator), with zero failures/skips
and empty parents afterward. LLVM/libc++20 actual-TU syntax, changed-file
cpplint and diff checks passed. Parallel compilation temporarily left assembly
in the controlled parent at lint/syntax receipt time; runtime receipts confirm
complete cleanup after compile completion.

The single repaired-helper process passed on real Windows NT 10.0.26200 with
LLVM-MinGW 20261020, clang 23.1.4, MinGW-w64 15.0 and UCRT: empty and file-bearing
directories absent immediately after destruction, payload absent, native exit
0, 146 ms, stderr empty. No removal control runs after the destructor; the
original absence postconditions must hold directly. Wrapper exit 0 and its
owned Windows temporary parent removed. The extracted helper is byte-verbatim
from the changed owning TU, SHA256
`6b2af66cbaf5d68454059c7fd3ae7ada17ef409b680f68359f9656aa8a0a5fee`.
Source SHA256: `f47abdb95251fadeb9abd9695ebdd56739c69c86253fcda34a96f85ebbc1f9a6`.
Both original and fixed-helper native budgets are now closed with zero remaining
launches. This native/UCRT GREEN verifies the local mechanism; exact CI/MSYS2
post-fix proof remains pending and CI owned. The separate 180-second completion
UNKNOWN and every other closed investigation remain unchanged.

Caller full validation session `79331177cdaaac0246f4b911ff4c1873` sealed PASS:
one iteration, all nine independent reviewers approve, zero findings/fixes/unworked.
Eight retained successful gate receipts and exact source/helper hashes verified;
no checks rerun during review. Native original and fixed helper budgets remain
closed. Exact MSYS2 CI proof stays incomplete and CI/v3-PR owned.
