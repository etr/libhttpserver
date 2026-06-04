### TASK-062: RFC-7616-compliant Digest auth response factory

**Milestone:** M7 - v2 Cleanup
**Component:** `http_response::unauthorized`, `webserver_impl::dauth`
**Estimate:** L

**Goal:**
Make `http_response::unauthorized(...)` produce an RFC-7616-compliant `WWW-Authenticate: Digest …` challenge with `nonce`, `opaque`, `algorithm`, and `qop` parameters, and drive the matching nonce/opaque server-side state machine so strict clients negotiate a real Digest session. The current implementation (`src/httpserver/http_response.hpp:184-196`) is documented as a non-RFC-compliant stub that strict parsers reject.

**Action Items:**
- [ ] Audit current `http_response::unauthorized(...)` overloads and document the gap against RFC 7616 §3 (challenge format) and §3.4 (`Authorization` validation) in a short header comment.
- [ ] Add a nonce/opaque generator to `webserver_impl` (CSPRNG-backed, with replay/expiry tracking). Reuse the existing `dauth` plumbing where possible.
- [ ] Extend `http_response::unauthorized(...)` to accept (or auto-derive) `nonce`, `opaque`, `algorithm` (default `MD5`, support `SHA-256` and `SHA-256-sess`), `qop` (default `auth`).
- [ ] Wire the dispatch path to validate incoming `Authorization: Digest …` against the issued nonce/opaque pair and route to `dauth` handlers.
- [ ] Convert the six v2 digest placeholder integ tests (`test/integ/authentication.cpp:42-60, 245-613`) to drive the nonce/opaque state machine end-to-end. Coordinated with TASK-079 (test work).
- [ ] Update Doxygen on `unauthorized(...)` to remove the "non-RFC-compliant stub" disclaimer and add RFC references.

**Dependencies:**
- Blocked by: None
- Blocks: TASK-079

**Acceptance Criteria:**
- `curl --digest -u user:pass …` against an `unauthorized`-protected route negotiates successfully (no `400 Bad Request` from strict client parsers).
- A new integ test pins the `WWW-Authenticate` challenge format against RFC 7616 §3.3 examples.
- The six existing digest placeholder tests in `authentication.cpp` either drive real nonce/opaque flows (preferred) or are explicitly retired in favour of new tests.
- libmicrohttpd's MD5/SHA-256 helpers remain the underlying primitive — we do not roll our own.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-005 (`unauthorized` factory completeness)
**Related Decisions:** None new (RFC 7616)

**Status:** Backlog
