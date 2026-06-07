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

**Related Requirements:** PRD §2 API minimalism
**Related Decisions:** None new

**Status:** Done
