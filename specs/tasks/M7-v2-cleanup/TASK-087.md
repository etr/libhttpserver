### TASK-087: Restore msan CI lane

**Milestone:** M7 - v2 Cleanup
**Component:** `.github/workflows/verify-build.yml`
**Estimate:** M

**Goal:**
`.github/workflows/verify-build.yml:75-85` has the msan matrix entry commented
out (Ubuntu 18.04 / clang-6.0 retired) with no replacement. asan/lsan/tsan/ubsan
are all wired up; msan went silently absent.

**Action Items:**
- [ ] Choose a current msan-capable lane: Ubuntu 22.04 (or 24.04) with clang ≥ 16, building against an instrumented libc++ (`-stdlib=libc++ -fsanitize=memory -fsanitize-memory-track-origins`). Build the instrumented stdlib once and cache it across runs.
- [ ] Add the lane to `verify-build.yml` matrix, mirroring the asan/tsan job structure (env vars, `-fno-omit-frame-pointer`, etc.).
- [ ] Triage any new findings: each must be either fixed or suppressed in `test/msan.supp` with a comment naming the upstream issue.
- [ ] Wire the suppressions file into the lane like `tsan.supp` already is.
- [ ] Remove the commented-out msan block at lines 75-85.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- A green msan lane runs on every PR.
- `test/msan.supp` (if needed) is narrow — no wildcards over libhttpserver symbols.
- The audit's "msan silently gone" finding is closed.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 security NFR, PRD §3.6 sanitizer-clean
**Related Decisions:** None new

**Status:** Backlog
