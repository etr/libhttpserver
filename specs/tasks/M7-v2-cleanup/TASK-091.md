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
- [x] `check-soversion.sh`: install `readelf` / `otool` as a hard prerequisite (CI tooling) rather than degrading silently. If a CI lane truly cannot install them, fail with a clear error instead of running the filename-only fallback. (Also promoted the A6 pkg-config-absent fallback to a hard `fail`, per the goal statement's "an absent tool fails the script".)
- [x] `check-readme.sh` + `check-release-notes.sh`: flip markdownlint to `STRICT=yes` by default; require an explicit `LIBHTTPSERVER_MARKDOWNLINT_ADVISORY=1` env var to downgrade. (Default polarity flipped; legacy `MARKDOWNLINT_STRICT` still honored, default now `yes`. `check-release-notes.sh` now surfaces findings via `2>&1` instead of swallowing them.)
- [x] `check-readme.sh`: fix the fence-balance check to track open/close *pairs* in order rather than counting parity. Add a unit test (a fixture markdown file with two consecutive openers) that asserts the new check fails on it. (Extracted `scripts/lib/check-fence-balance.sh` — an ordered state machine — shared by both README and RELEASE_NOTES checks; unit test `scripts/test_check_fence_balance.sh`, wired as `lint-fence-balance` into check-local.)
- [x] `check-release-notes.sh`: enumerate the full `no_*` setter family so removals are non-optional. **DEVIATION (approved):** the literal instruction — scan `src/httpserver/create_webserver.hpp` for `[[deprecated]]` markers — is infeasible: v2 removed the `no_*` setters outright and never marked any `[[deprecated]]` (zero such markers, zero `no_*` setters remain in the v2 header). Instead the full family (17 names) is committed to `scripts/lib/v1-no-setters.txt` (single source of truth, derivation documented in its header), read by both `check-release-notes.sh` (A2 presence) and `check-readme.sh` (forbidden set). A live `git show master:...` scan was rejected as lane-unsafe on shallow CI checkouts. RELEASE_NOTES.md's "What's gone" list was expanded to cover all 17 so A2 is green.
- [x] `check-examples.sh`: re-include `client_cert_auth` in the `noinst_PROGRAMS` coverage check; fix the root cause if the program legitimately can't be built on a given lane. (Root-cause fix: moved into the `HAVE_GNUTLS` conditional in `examples/Makefile.am` — it builds and links cleanly there, verified. `KNOWN_ARTIFACTS` emptied; `verify-installed-examples.sh` `should_skip()` kept in sync.)
- [x] `check-parallel-install.sh` + `check-soversion.sh`: add `-e` to the `set` line. Update the inline comment explaining the choice. (Guarded the audited command-substitution / tee-pipeline sites so `-e` does not abort spuriously; also fixed `scripts/lib/resolve-prefix.sh`, which both scripts source.)

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

**Status:** Done
