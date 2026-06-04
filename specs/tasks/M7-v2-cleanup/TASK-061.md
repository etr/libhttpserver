### TASK-061: Mechanical cleanup sweep — unfinished prose, orphan comments, stale doc references

**Milestone:** M7 - v2 Cleanup
**Component:** Various (`src/`, `test/`, `scripts/`)
**Estimate:** S

**Goal:**
Clear the mechanical leftovers flagged in the audit's "Proposed disposition (next steps)" — short, low-risk edits that should land together so reviewers can scan one PR for stale prose drift.

**Action Items:**
- [x] Finish the truncated comment at `src/detail/webserver_register.cpp:344-349`. The TASK-029 block comment ends mid-sentence (`"…keeps working at the daemon level, but"` then closing brace). Either complete the sentence with the original intent (reconstruct from git history or surrounding code) or rewrite the comment to stand on its own.
- [x] Remove the two orphan comment fragments at `src/webserver.cpp:503-504` (leftover from removed logic).
- [x] Update the stale `XFAIL_TESTS` comment block at `test/Makefile.am:67-74` to reflect that `header_hygiene` was removed when TASK-020 landed (cross-referenced at lines 532-535 in the same file).
- [x] Drop the stale `RELEASE_NOTES.md) continue ;;  # created by TASK-042, not yet present` line at `scripts/check-readme.sh:273` — TASK-042 has shipped.
- [ ] Remove the decade-old `//TODO: personalized messages` at `test/littletest.hpp:21` only if we have a fork policy that lets us; otherwise leave (it's vendored).

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- `git diff master..HEAD -- src/detail/webserver_register.cpp` shows a complete, well-formed comment.
- `src/webserver.cpp:503-504` no longer contains orphan comment fragments.
- `test/Makefile.am:67-74` no longer claims `header_hygiene` is in `XFAIL_TESTS`.
- `scripts/check-readme.sh:273` no longer carries the "created by TASK-042, not yet present" line.
- `make check` still green.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 code quality NFR
**Related Decisions:** None new

**Status:** Done
