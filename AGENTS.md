# v3 task workflow

- Use Groundwork for v3 task implementation, validation, and finalization.
- Create v3 task branches from `v3` and merge completed work into `v3`.
- Local builds and tests gate local task completion and merge. User clarification
  on 2026-10-06 assigns BSD, Windows, and other nonlocal platform checks to CI
  and the v3 PR; missing nonlocal evidence does not block local task completion.
  This policy takes precedence over task cards or saved plans that require
  nonlocal platform receipts before merging. Record unexecuted checks honestly
  and address platform failures during the v3 PR.
- Preserve unrelated work and use isolated task worktrees. Merge and clean up
  the current task before starting the next task in the serial batch.
