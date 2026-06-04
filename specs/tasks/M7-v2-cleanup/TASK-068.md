### TASK-068: `connection_state` hardening — CWE-226 arena overflow zeroing + CWE-14 `explicit_bzero`

**Milestone:** M7 - v2 Cleanup
**Component:** `src/httpserver/detail/connection_state.hpp`
**Estimate:** S

**Goal:**
Two acknowledged residual gaps at `connection_state.hpp:122, 130`:
1. CWE-226 — arena overflow bytes from the per-connection scratch region are not zeroed on connection reuse, so a later request can observe trailing bytes from a prior request's body.
2. CWE-14 — the current clear path uses `memset(…, 0, …)`, which an optimizer may dead-code-eliminate. Replace with `explicit_bzero` (or a portable equivalent) so the zeroing is observable.

**Action Items:**
- [ ] In `connection_state::reset()` (or whatever clears the arena between requests), zero the entire used-bytes prefix unconditionally. Document the trade-off (cycles vs. CWE-226 mitigation) in the header.
- [ ] Replace the `memset` call with `explicit_bzero` on glibc/musl/macOS, `SecureZeroMemory` on Windows, or a hand-rolled `volatile`-pointer loop where neither is available. Centralize the helper in `src/httpserver/detail/secure_zero.hpp`.
- [ ] Update the inline comments at lines 122 and 130 to reference the fix.
- [ ] Add a unit test (compile-time + runtime) that pins the helper is not dead-code-eliminated under `-O2` (using a memory barrier and a `volatile` sink to observe the write).

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- A request body containing sentinel bytes (e.g. `0xDEADBEEF` pattern) is not observable from a subsequent request on the same MHD connection, verified by an integ test.
- The portable secure-zero helper compiles on every CI lane (Linux glibc, Linux musl, macOS, Windows).
- ASan + valgrind continue to report clean.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 security NFR (CWE-226, CWE-14)
**Related Decisions:** None new

**Status:** Backlog
