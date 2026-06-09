### TASK-076: Replace tautological-pass blocks in TLS test lanes

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/ws_start_stop.cpp`
**Estimate:** M

**Goal:**
`ws_start_stop.cpp` carries ~20 `try { ws.start() } catch { LT_CHECK_EQ(1,1); return; }` blocks across SSL/mTLS/PSK/SNI sections, plus 5 more `if (!has_gnutls_cli()) { LT_CHECK_EQ(1,1); return; }` env-gates. On hosts without GnuTLS or `gnutls-cli`, the entire TLS suite reduces to `LT_CHECK_EQ(1,1)` passes — a build that silently lost TLS support would still report green. Replace these with a real fail-fast or skip mechanism that distinguishes "configuration not supported" from "configuration broken".

**Action Items:**
- [x] Audit each `LT_CHECK_EQ(1,1)` at the lines listed in the audit (759, 789, 914, 949, 1016, 1051, 1091, 1129, 1171, 1212, 1266, 1367, 1486, 1512, 1537, 1552, 1599, 1642, 1685, 1728, plus env-gates at 1550, 1597, 1640, 1683, 1726). The implementation grepped the full set at start-of-cycle and replaced all 33 matches (the plan audit covered 8 lines the original task enumeration omitted).
- [x] For each, classify:
  - (a) Environment lacks TLS dependencies → emitted `LT_SKIP_IF(...)` via the new littletest macro (added to `test/littletest.hpp`). Runner reports `[SKIP]`, not `PASS`; a binary whose only outcomes are SKIPs exits 77 (Automake SKIP).
  - (b) Environment supports TLS but a specific cipher/PSK config is rejected → typed catch on `std::exception` + substring-match on `is_psk_unsupported_error(e)` for SKIP, otherwise `throw;` so implementation-broken cases fail loudly.
- [x] Added a CI matrix lane (`build-type: tls-no-cli`) that runs `ws_start_stop.cpp` with `libgnutls28-dev` installed but `gnutls-bin` deliberately absent; the post-test verification step greps `test/ws_start_stop.log` for ≥5 `[SKIP]` markers (the five `psk_connection_*` tests) and zero `[CHECK/ASSERT FAILURE]` lines. A separate canary step on the baseline `classic` lane asserts those tests do NOT report SKIP when `gnutls-bin` is present.
- [x] Added RELEASE_NOTES.md "Test infrastructure" section documenting `LT_SKIP` / `LT_SKIP_IF` and the new exit-77 semantics.

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

**Status:** Done
