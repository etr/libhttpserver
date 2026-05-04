### TASK-019: High-level GnuTLS accessors replacing `gnutls_session_t`

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** `http_request` TLS surface
**Estimate:** L

**Goal:**
Replace methods that returned raw `gnutls_session_t` (or other GnuTLS types) with high-level accessors so the public header doesn't need `<gnutls/gnutls.h>`.

**Action Items:**
- [ ] Remove any public `http_request` method returning `gnutls_session_t`.
- [ ] Add high-level accessors (return `string_view` or sentinel when TLS disabled):
  - `bool has_tls_session() const noexcept;`
  - `bool has_client_certificate() const noexcept;`
  - `string_view get_client_cert_dn() const;`
  - `string_view get_client_cert_issuer_dn() const;`
  - `string_view get_client_cert_cn() const;`
  - `string_view get_client_cert_fingerprint_sha256() const;` (hex-encoded)
  - `bool is_client_cert_verified() const noexcept;`
  - `std::int64_t get_client_cert_not_before() const noexcept;` (seconds since epoch; -1 if no cert)
  - `std::int64_t get_client_cert_not_after() const noexcept;`
- [ ] Implementation uses GnuTLS internally (in `http_request.cpp`); `gnutls_session_t` remains accessible to library internals via friend access on the impl.
- [ ] When `HAVE_GNUTLS` is off at build time, all accessors return empty / `false` / `-1` (no exception, per §7).

**Dependencies:**
- Blocked by: TASK-015
- Blocks: TASK-020, TASK-034

**Acceptance Criteria:**
- `grep -E '#include\s+<gnutls/' src/httpserver/*.hpp` returns nothing.
- `grep -E 'gnutls_session_t' src/httpserver/*.hpp` returns nothing.
- Existing TLS tests still pass; an additional test exercises the new accessors against a known client cert and verifies returned DN/CN/fingerprint.
- TLS-disabled build returns empty/sentinel values without throwing.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDR-REQ-003, PRD-FLG-REQ-002
**Related Decisions:** §4.2, §6.2

**Status:** Not Started
