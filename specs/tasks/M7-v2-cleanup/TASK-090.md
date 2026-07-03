### TASK-090: Harden CodeQL workflow and Codecov upload

**Milestone:** M7 - v2 Cleanup
**Component:** `.github/workflows/codeql-analysis.yml`, `.github/workflows/verify-build.yml`
**Estimate:** S

**Goal:**
Two CI workflow weaknesses called out in the audit:
- `.github/workflows/codeql-analysis.yml` uses deprecated/unpinned `@v1`/`@v2` actions; commented-out Autobuild scaffolding never replaced. Not hardened to match `verify-build.yml`'s SHA-pinning.
- `verify-build.yml:982` — `fail_ci_if_error: false` on the Codecov upload; coverage failures are non-blocking.

**Action Items:**
- [x] In `codeql-analysis.yml`: replace `@v1`/`@v2` action references with full commit SHAs (matching the pattern in `verify-build.yml`). List each action + its current upstream tag in a header comment for rotation.
- [x] Replace the commented-out Autobuild scaffolding with the explicit configure + make build steps. CodeQL's database extraction needs the actual compile commands, not a guess.
- [x] In `verify-build.yml:982`: flip `fail_ci_if_error: true` so a Codecov upload failure breaks the CI job. Distinguish from "coverage dropped" — that's a separate gate.
- [x] Document the Windows-doxygen invariance gap from `verify-build.yml:382-388` (Windows lane intentionally excludes doxygen+graphviz). Either move the doxygen invariant into a cross-platform check or add a `verify-build.yml` comment explaining why it stays Linux-only.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- All GitHub Actions in both workflows are pinned to commit SHAs.
- CodeQL builds via explicit configure + make, not autobuild.
- Codecov upload failures fail CI.
- Doxygen invariance is either cross-platform or carries a documented exclusion.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 supply-chain NFR
**Related Decisions:** None new

**Status:** Backlog
