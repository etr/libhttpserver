### TASK-060: Scope or remove file-scoped `-Warray-bounds` suppressions

**Milestone:** M7 - v2 Cleanup
**Component:** `src/http_utils.cpp`, `src/detail/ip_representation.cpp`
**Estimate:** S

**Goal:**
Eliminate the two unscoped `#pragma GCC diagnostic ignored "-Warray-bounds"` directives flagged as the worst-shaped suppressions in `specs/tasks/v2-branch-gap-audit.md` §1 "HIGH — Unscoped warning suppressions". Either localize each to the minimum offending line range with a `push`/`pop` pair plus a comment explaining the underlying false-positive, or investigate and remove the suppression entirely once the warning is silenced at the source.

**Action Items:**
- [ ] Reproduce the `-Warray-bounds` warning at `src/http_utils.cpp:62` under the supported GCC versions used in CI (matrix lanes in `.github/workflows/verify-build.yml`). Capture the exact diagnostic text and offending construct in the commit message.
- [ ] Reproduce the same at `src/detail/ip_representation.cpp:55`. Capture diagnostic text.
- [ ] For each site: either (a) rewrite the source so the warning no longer fires and delete the pragma, or (b) replace the file-scoped pragma with a tightly scoped `#pragma GCC diagnostic push` / `#pragma GCC diagnostic ignored "-Warray-bounds"` / `#pragma GCC diagnostic pop` block around the minimum offending lines, with a comment naming the GCC version range and the upstream report (if any).
- [ ] If the suppression is kept, add a `TODO(TASK-NNN-followup)` style comment only if a concrete follow-up exists; otherwise the scoping comment is sufficient.
- [ ] Add a guard test or static-analysis spot-check that fails if either file regains a file-scoped suppression.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- `grep -nE '^#pragma GCC diagnostic ignored "-Warray-bounds"' src/http_utils.cpp src/detail/ip_representation.cpp` returns no matches at file scope (only inside `push`/`pop` blocks, if any).
- The verify-build.yml GCC lanes still pass without `-Warray-bounds` warnings.
- Each remaining suppression carries a comment naming the GCC version range and the reason.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 code quality NFR
**Related Decisions:** None new

**Status:** Backlog
