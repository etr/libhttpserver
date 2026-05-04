### TASK-034: Build-flag-independent public API + `webserver::features()`

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Feature availability
**Estimate:** M

**Goal:**
Remove `#ifdef HAVE_*` from public headers and provide runtime feature reporting plus documented sentinel/throw behavior when a build-disabled feature is invoked.

**Action Items:**
- [ ] Remove `#ifdef HAVE_BAUTH | HAVE_DAUTH | HAVE_GNUTLS | HAVE_WEBSOCKET` guards from every public header — the methods are now declared unconditionally.
- [ ] Implementation files: when the relevant `HAVE_*` is undefined, the implementation either returns the documented sentinel (empty `string_view`, `false`, `-1`) or throws `feature_unavailable` per §7.
- [ ] Add `webserver::features()` returning `struct features { bool basic_auth; bool digest_auth; bool tls; bool websocket; };`. Implementation reads compile-time `HAVE_*` and returns a value.
- [ ] `create_webserver::use_ssl(true)` on a non-TLS build throws `feature_unavailable` at `webserver` construction time (consistent across all features per §7).
- [ ] `register_ws_resource` on a non-WebSocket build throws `feature_unavailable`.
- [ ] Confirm `feature_unavailable.what()` always names both feature and the controlling flag (TASK-003 invariant).

**Dependencies:**
- Blocked by: TASK-003, TASK-019, TASK-033
- Blocks: TASK-035, TASK-037, TASK-043

**Acceptance Criteria:**
- `grep -E '#if(def)? HAVE_(BAUTH|DAUTH|GNUTLS|WEBSOCKET)' src/httpserver/*.hpp` returns 0 (PRD §3.2 acceptance).
- A consumer source file compiles unchanged against TLS-on and TLS-off builds (TASK-036 verifies this in CI).
- A test on a TLS-disabled build asserts `webserver.features().tls == false` and that calling `create_webserver().use_ssl(true).build()` throws `feature_unavailable` whose `what()` mentions both `tls` and `HAVE_GNUTLS`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-FLG-REQ-001..005
**Related Decisions:** §7

**Status:** Not Started
