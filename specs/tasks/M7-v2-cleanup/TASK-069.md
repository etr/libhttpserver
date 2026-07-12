### TASK-069: Remove transitional two-arg `http_request_impl` constructor

**Milestone:** M7 - v2 Cleanup
**Component:** `src/httpserver/detail/http_request_impl.hpp`
**Estimate:** S

**Goal:**
The two-arg `http_request_impl` constructor at `http_request_impl.hpp:95-99` is preserved "for source compatibility" with no removal date. Remove it now that the v2 cutover is complete, so the only constructor is the canonical one. Internal headers do not need a deprecation cycle.

**Action Items:**
- [x] Grep `src/` and `test/` for callers of the two-arg form. Migrate each to the canonical form.
- [x] Delete the two-arg overload and its comment block.
- [x] Verify `make check` is green across all build-flag matrix lanes (HAVE_BAUTH, HAVE_DAUTH, HAVE_GNUTLS, HAVE_WEBSOCKET).

**Dependencies:**
- Blocked by: TASK-015 (request impl PIMPL split, Done)
- Blocks: None

**Acceptance Criteria:**
- The two-arg constructor and its forwarding/initialization no longer exist.
- All callers use the canonical constructor.
- Typecheck passes.
- Tests pass.

**Verification:**
- `make check -j1` (release build): 98/98 pass.
- `make check -j1` (`--enable-debug`, `-Werror -Wextra`): 98/98 pass.
- Header-only cross-flag sweep: the sentinel TU (the new static_asserts
  in `test/unit/http_request_pimpl_test.cpp`) compiles cleanly under
  each of `-UHAVE_BAUTH` / `-UHAVE_DAUTH` / `-UHAVE_GNUTLS` /
  `-UHAVE_WEBSOCKET`, proving the canonical constructor's
  `#ifdef`-conditioned member-init list still works under every
  flag-off permutation (satisfies action item 3). See commit `2a37e9e`
  for the full verification log.

**Related Requirements:** PRD §2 API minimalism
**Related Decisions:** None new

**Status:** Done
