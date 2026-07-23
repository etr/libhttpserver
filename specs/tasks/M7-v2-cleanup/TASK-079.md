### TASK-079: Drive nonce/opaque state machine in v2 digest-auth integ tests

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/authentication.cpp`
**Estimate:** M

**Goal:**
The six v2 digest-auth tests (`digest_auth_wrong_pass`,
`digest_auth_with_ha1_md5[_wrong_pass]`, `digest_auth_with_ha1_sha256[_wrong_pass]`,
`digest_user_cache_with_auth`) at `test/integ/authentication.cpp:42-60, 245-613`
are observationally indistinguishable under v2 because the nonce/opaque state
machine isn't driven. They are retained as static-challenge pins. Convert them
into real two-round flows once TASK-062 lands the RFC-7616-compliant challenge.

**Action Items:**
- [x] Build a small helper in the test harness that parses a `WWW-Authenticate: Digest …` response into a structured challenge (nonce, opaque, algorithm, qop), computes the client-side response per RFC 7616 §3.4, and issues the second request.
- [x] Convert each of the six tests to a two-round shape: first request → `401` with challenge → compute response → second request → expected status (`200` for valid creds, `401`/`403` for wrong creds).
- [x] Cover both `MD5` and `SHA-256` algorithms in matched pairs.
- [x] Replace the existing static-challenge assertions with end-to-end behaviour pins.

**Dependencies:**
- Blocked by: TASK-062 (RFC-7616 challenge factory)
- Blocks: None

**Acceptance Criteria:**
- All six tests exercise a real two-round challenge/response flow.
- Wrong-password variants assert `401` after the second request, not just on the first.
- HA1-precomputed variants assert that the server accepts the precomputed credential and does not recompute MD5/SHA-256 on the secret.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-005 (`unauthorized` factory completeness)
**Related Decisions:** None new (RFC 7616)

**Status:** Done
