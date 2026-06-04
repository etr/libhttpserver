### TASK-089: Wire `check-parallel-install` into per-PR CI + remove SKIP-degrades-to-pass paths

**Milestone:** M7 - v2 Cleanup
**Component:** `scripts/check-parallel-install.sh`, `.github/workflows/verify-build.yml`
**Estimate:** M

**Goal:**
`scripts/check-parallel-install.sh:36-38` is explicitly opt-in via
`make check-parallel-install` and is not wired into per-PR CI. It also
degrades to `SKIP` (exit 0) on five distinct environment-quirk paths
(lines 154, 162, 170, 199, 207). The combination means the parallel-install
acceptance gate from TASK-044 never actually gates anything in CI.

**Action Items:**
- [ ] Add a `verify-build.yml` job that runs `make check-parallel-install` after the standard `make check` step on the Linux lane (gcc + libstdc++, the configuration most users hit).
- [ ] Walk each of the five SKIP paths in the script (master ref missing, git worktree add failed, v1 bootstrap failed, v1 configure failed, v1 make failed). For each: if the environment quirk is genuinely outside our control (e.g., a shallow clone on a CI runner), still emit a clear `SKIP` but fail the overall job unless an env var explicitly authorizes the skip (`HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1`). If the quirk is fixable (e.g., the script's git fetch logic), fix it.
- [ ] Document the CI integration in `RELEASE_NOTES.md` under "Test infrastructure" so reviewers can verify the gate is live.
- [ ] Update the inline comment block of the script to remove the "opt-in only" framing.

**Dependencies:**
- Blocked by: TASK-044 (Done)
- Blocks: None

**Acceptance Criteria:**
- A PR that breaks parallel-installability of `libhttpserver1` + `libhttpserver2` fails the new CI job.
- The five SKIP paths still exist but flip the overall job to `failure` unless `HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1` is set.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §1 release strategy, PRD §2 backwards-compat NFR
**Related Decisions:** DR-011 (SOVERSION-only)

**Status:** Backlog
