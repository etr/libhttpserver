# FEATURE-ci-freebsd-evidence-receiver: recover FreeBSD package evidence into a fresh receiver

Status: Implementation complete; caller validation and finalization pending.
Base: `v3` at `b2ee4a2bd2a779921c9200a6afa8bc222fcc64d2`.
Plan: `.groundwork-plans/FEATURE-ci-freebsd-evidence-receiver-plan.md` in the primary checkout.
Journal: `.debug/ci-freebsd-scp-overlay-w5cb2jxm/journal.md` in the primary checkout.

Action items:

- [x] Reuse the closed bounded SCP diagnosis without reopening investigation.
- [x] Create a fresh isolated task worktree from current `v3`; preserve other worktrees.
- [x] Reproduce the recovery shell's permission failure with actual local OpenSSH
  sender/receiver processes, an owned readonly target, and an existing destination symlink.
- [x] Allocate a fresh owned receiver with `mktemp` under `RUNNER_TEMP` and copy the
  unchanged guest evidence root into it.
- [x] Upload that copied root on success with the existing artifact identity and
  relative payload layout. Preserve automatic rsync evidence as the failure fallback.
- [x] Propagate the original SCP failure; keep partial copies out of the fallback.
- [x] Verify original link/target bytes and readonly permissions remain intact.
- [x] Run affected recovery contracts, workflow contracts, actionlint, Python/Bash
  syntax checks, and whitespace checks.
- [ ] Caller: Groundwork validation, finalization, merge and cleanup.
- [ ] Native FreeBSD post-fix recovery: next source CI / v3 PR owns this check.

The existing guest EXIT copy and automatic rsync copyback can leave absolute
Python symlinks in the host `package-evidence` tree. The later legacy SCP receiver
overlaid that tree and could follow those links into an unwritable destination.
The closed diagnosis establishes this mechanism locally and corroborates the CI
failure; actual failed-run host `lstat` evidence was not retained. The subsequent
package run `40245167124`, job `121325086062`, corroborates the same Python-path
permission error. Neither failed native job is reclassified as passing.

The recovery step now publishes the existing `package-evidence` fallback first,
creates a unique receiver under the runner's temporary directory, and selects
the copied `package-evidence` root only after successful SCP. On copy failure it
exits with that command's status while the always-run upload retains the original
rsync tree. Allocation failure likewise retains the fallback. No existing evidence
is deleted, chmodded, overwritten or traversed by the new copy destination.

TDD and local verification:

- Two new tests execute the extracted production recovery shell, replacing only
  SSH transport with a local process while using real OpenSSH SCP peers. Before
  the workflow repair, the owned symlink case exits 1 with `Permission denied`,
  and a failed partial copy contaminates the fallback. After the repair both pass.
- Successful recovery verifies byte-exact binary payload and matrix receipt,
  readonly mode, the unchanged absolute fallback symlink and protected sentinel,
  and the selected upload root under a temporary path containing spaces.
- An actual dangling source link makes SCP exit 1 after a partial transfer. The
  step retains that error and selects the unchanged rsync fallback; the partial
  payload remains in the separate receiver.
- `python3 scripts/test-v3-ci-workflows.py FreeBSDNodePrerequisites -v`: 9 pass,
  including existing package/archive failure receipts and payload layout checks.
- `python3 scripts/test-v3-package-workflow.py -v`: 4 pass.
- `/tmp/v3-ci-actionlint/actionlint .github/workflows/v3-package-validation.yml`:
  pass. Changed Python compiles, extracted recovery Bash parses, and
  `git diff --check` passes. CLAUDE's C++ lint does not apply to these files.

Raw RED/GREEN output is retained under the journal directory's `implementation/`.
This proof uses no network, FreeBSD VM, original native program, full baseline,
provider/SDK rebuild, CI dispatch, push or Groundwork validation. The guest native
commands, checks, archive workflow and artifact identity remain unchanged. Native
post-fix recovery and overall CI-green status remain incomplete and CI-owned;
repository policy permits local completion without nonlocal platform receipts.
