### TASK-068: `connection_state` hardening — CWE-226 arena overflow zeroing + CWE-14 `explicit_bzero`

**Milestone:** M7 - v2 Cleanup
**Component:** `src/httpserver/detail/connection_state.hpp`
**Estimate:** S

**Goal:**
Two acknowledged residual gaps at `connection_state.hpp:122, 130`:
1. CWE-226 — arena overflow bytes from the per-connection scratch region are not zeroed on connection reuse, so a later request can observe trailing bytes from a prior request's body.
2. CWE-14 — the current clear path uses `memset(…, 0, …)`, which an optimizer may dead-code-eliminate. Replace with `explicit_bzero` (or a portable equivalent) so the zeroing is observable.

**Action Items:**
- [x] In `connection_state::reset_arena()`, zero the entire `initial_buffer_` unconditionally via the new `httpserver::detail::secure_zero` helper. The trade-off (a few thousand cycles per keep-alive request for a non-elidable byte-wise clear vs. the CWE-14 + CWE-226 mitigation) is documented in the rewritten comment block at lines 106-148.
- [x] Replace the `memset` call with the `secure_zero` helper centralized in `src/httpserver/detail/secure_zero.hpp`. The helper dispatches at compile time to `explicit_bzero` (glibc/musl/BSD), `RtlSecureZeroMemory` (Windows), or a portable `volatile`-pointer loop + `asm __volatile__("" ::: "memory")` clobber fallback (macOS + any lane without `explicit_bzero`). Note: `memset_s` was evaluated and skipped -- its `__STDC_WANT_LIB_EXT1__` include-order requirement is incompatible with a transitively-included internal header, so macOS takes the portable fallback (same security guarantee without the preprocessor-order coupling).
- [x] Updated the inline comments at the formerly-line-122 / formerly-line-130 sites (now lines 106-148 of `connection_state.hpp`) to reference the `secure_zero` helper and the CWE-14 / CWE-226 posture. Also updated the cross-reference comment at `src/detail/webserver_callbacks.cpp:124`.
- [x] Added `test/unit/secure_zero_dce_test.cpp` -- compiled at `-O2 -DNDEBUG` (per-target `CXXFLAGS` override), prefills a 256-byte buffer with 0xA5, calls `secure_zero`, then reads every byte through a `volatile unsigned char sink` and asserts each is zero. Also pinned `secure_zero(nullptr, 0)` and `secure_zero(p, 0)` no-op contracts.
- [x] Added `test/unit/connection_state_sentinel_test.cpp` -- prefills the full 8 KiB arena with 0xDE, calls `reset_arena()`, and asserts every byte is zero (CWE-226 unit pin). A companion test confirms that a sentinel pattern written into a 512-byte arena allocation is scrubbed after `reset_arena()` + same-size re-allocation.
- [x] Added `test/integ/connection_state_body_residue_test.cpp` -- the acceptance-criterion integ test. Two `GET /peek` requests over a single curl-keep-alive connection; the first carries a `DEADBEEFCRED_SENTINEL_USERNAME_FROM_REQUEST_1` basic-auth username (decoded into the arena-backed `http_request_impl::username` pmr::string), the second carries no Authorization header. The handler peeks `connection_state::initial_buffer_` via a new `HTTPSERVER_COMPILATION`-gated `http_request::underlying_connection_for_testing()` accessor. Sanity: request 1's handler observes the sentinel; headline: request 2's handler does not. Belt-and-braces: a server-wide `connection_opened` hook fires exactly once across both requests.

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

**Status:** Completed
