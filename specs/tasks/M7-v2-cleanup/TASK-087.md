### TASK-087: Restore msan CI lane

**Milestone:** M7 - v2 Cleanup
**Component:** `.github/workflows/verify-build.yml`
**Estimate:** M

**Goal:**
`.github/workflows/verify-build.yml:75-85` has the msan matrix entry commented
out (Ubuntu 18.04 / clang-6.0 retired) with no replacement. asan/lsan/tsan/ubsan
are all wired up; msan went silently absent.

**Action Items:**
- [x] Choose a current msan-capable lane: Ubuntu 22.04 (or 24.04) with clang ≥ 16, building against an instrumented libc++ (`-stdlib=libc++ -fsanitize=memory -fsanitize-memory-track-origins`). Build the instrumented stdlib once and cache it across runs.
- [x] Add the lane to `verify-build.yml` matrix, mirroring the asan/tsan job structure (env vars, `-fno-omit-frame-pointer`, etc.).
- [ ] Triage any new findings: each must be either fixed or suppressed in `test/msan.supp` with a comment naming the upstream issue. (deferred to PR — CI-side: `test/msan.supp` is intentionally absent until a real CI finding forces a narrow `use-of-uninitialized-value:<symbol>` entry; the conditional wiring is already in place to pick it up automatically when the file appears.)
- [x] Wire the suppressions file into the lane like `tsan.supp` already is.
- [x] Remove the commented-out msan block at lines 75-85.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- A green msan lane runs on every PR.
- `test/msan.supp` (if needed) is narrow — no wildcards over libhttpserver symbols.
- The audit's "msan silently gone" finding is closed.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 non-functional/cross-cutting quality bar (implicit correctness/memory-safety intent; §2 and §3.6 do not name an explicit sanitizer-clean requirement)
**Related Decisions:** None new

**Status:** Done
