### TASK-076: Replace tautological-pass blocks in TLS test lanes

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/ws_start_stop.cpp`
**Estimate:** M

**Goal:**
`ws_start_stop.cpp` carries ~20 `try { ws.start() } catch { LT_CHECK_EQ(1,1); return; }` blocks across SSL/mTLS/PSK/SNI sections, plus 5 more `if (!has_gnutls_cli()) { LT_CHECK_EQ(1,1); return; }` env-gates. On hosts without GnuTLS or `gnutls-cli`, the entire TLS suite reduces to `LT_CHECK_EQ(1,1)` passes — a build that silently lost TLS support would still report green. Replace these with a real fail-fast or skip mechanism that distinguishes "configuration not supported" from "configuration broken".

**Action Items:**
- [ ] Audit each `LT_CHECK_EQ(1,1)` at the lines listed in the audit (759, 789, 914, 949, 1016, 1051, 1091, 1129, 1171, 1212, 1266, 1367, 1486, 1512, 1537, 1552, 1599, 1642, 1685, 1728, plus env-gates at 1550, 1597, 1640, 1683, 1726).
- [ ] For each, classify:
  - (a) Environment lacks TLS dependencies → use the littletest `LT_SKIP_IF(!has_gnutls(), "gnutls not available")` (or equivalent) macro that the runner reports as `SKIP`, not `PASS`. Add such a macro if littletest does not have one.
  - (b) Environment supports TLS but a specific cipher/PSK config is rejected → assert specifically on that rejection rather than swallowing all exceptions; the test must distinguish "expected reject" from "implementation broken".
- [ ] Add a CI matrix lane that runs `ws_start_stop.cpp` on a host *with* GnuTLS + `gnutls-cli` and a separate lane that asserts the suite reports `SKIP` (not `PASS`) when they are absent.
- [ ] RELEASE_NOTES.md "Test infrastructure" note (if applicable) on the new skip semantics.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- `grep -nE 'LT_CHECK_EQ\(1, *1\)' test/integ/ws_start_stop.cpp` returns no matches.
- Running the suite without `gnutls-cli` reports `SKIP` for the affected blocks, not `PASS`.
- Running the suite with `gnutls-cli` exercises every block.
- A CI lane gates on "no `LT_CHECK_EQ(1,1)` regression" in `ws_start_stop.cpp`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 test reliability NFR
**Related Decisions:** None new

**Status:** Backlog
