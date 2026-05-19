### TASK-037: CI test for build-flag invariance

**Milestone:** M6 - Release Readiness
**Component:** CI / Test infrastructure
**Estimate:** S

**Goal:**
Lock in the "same consumer source compiles against TLS-on and TLS-off" invariant with a CI gate.

**Action Items:**
- [ ] Add a CI matrix job that builds the library twice: once with `--enable-tls --enable-bauth --enable-dauth --enable-websocket`, once with all four disabled.
- [ ] In each configuration, compile a single shared consumer fixture (e.g., `test/consumer_fixture.cpp`) that touches every feature-gated method: `req.get_user()`, `req.get_client_cert_dn()`, `ws.register_ws_resource(...)`, `cw.use_ssl(true)`, etc.
- [ ] Assert the fixture compiles in both configurations without source changes.
- [ ] Wire the matrix into the project's CI (Travis / GitHub Actions / whatever is present).

**Dependencies:**
- Blocked by: TASK-034
- Blocks: None

**Acceptance Criteria:**
- The CI matrix job is green in both configurations.
- An intentional regression (re-introducing `#ifdef HAVE_GNUTLS` around a public method) makes the matrix red.
- Typecheck passes.

**Related Requirements:** PRD-FLG-REQ-001
**Related Decisions:** §9 testing item 2

**Status:** Not Started
