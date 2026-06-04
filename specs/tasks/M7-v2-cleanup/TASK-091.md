### TASK-091: Tighten CI script soft-degradations

**Milestone:** M7 - v2 Cleanup
**Component:** `scripts/check-*.sh`
**Estimate:** M

**Goal:**
A cluster of soft degradations in CI helper scripts that silently weaken gates:
- `scripts/check-soversion.sh:193, 210, 249` — SONAME assertions degrade to filename-only when `readelf`/`otool` is absent; pkg-config check degrades to literal Version-line grep.
- `scripts/check-readme.sh:321-331` + `scripts/check-release-notes.sh:348-353` — markdownlint findings are advisory-only; only readme has a STRICT knob (defaults to "no").
- `scripts/check-readme.sh:294-297` — known fence-balance gap (two consecutive opening fences + two closing fences pass the even-count check).
- `scripts/check-release-notes.sh:107-109, 248-254` — intentionally partial coverage of the `no_*` setter family (spot-checked, not enumerated).
- `scripts/check-examples.sh:120` — `client_cert_auth` allow-listed out of the `noinst_PROGRAMS` coverage check.
- `scripts/check-parallel-install.sh:55` + `scripts/check-soversion.sh:48` — `set -uo pipefail` (no `-e`); opt-out of fail-on-error.

Land tighter defaults across the set.

**Action Items:**
- [ ] `check-soversion.sh`: install `readelf` / `otool` as a hard prerequisite (CI tooling) rather than degrading silently. If a CI lane truly cannot install them, fail with a clear error instead of running the filename-only fallback.
- [ ] `check-readme.sh` + `check-release-notes.sh`: flip markdownlint to `STRICT=yes` by default; require an explicit `LIBHTTPSERVER_MARKDOWNLINT_ADVISORY=1` env var to downgrade.
- [ ] `check-readme.sh`: fix the fence-balance check to track open/close *pairs* in order rather than counting parity. Add a unit test (a fixture markdown file with two consecutive openers) that asserts the new check fails on it.
- [ ] `check-release-notes.sh`: enumerate the full `no_*` setter family by scanning `src/httpserver/create_webserver.hpp` for `[[deprecated]]` markers and feeding the list into the check, so removals are non-optional.
- [ ] `check-examples.sh`: re-include `client_cert_auth` in the `noinst_PROGRAMS` coverage check; fix the root cause if the program legitimately can't be built on a given lane.
- [ ] `check-parallel-install.sh` + `check-soversion.sh`: add `-e` to the `set` line. Update the inline comment explaining the choice.

**Dependencies:**
- Blocked by: TASK-044 (Done)
- Blocks: None

**Acceptance Criteria:**
- All check-*.sh scripts run under `set -euo pipefail`.
- No silent degradations: an absent tool fails the script with a clear error.
- The fence-balance check catches the two-consecutive-openers regression.
- The `no_*` setter check enumerates from source, not a hand-written list.
- `client_cert_auth` is covered.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 CI hygiene NFR
**Related Decisions:** None new

**Status:** Backlog
