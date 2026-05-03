### 4.2 `http_request`

**Responsibility:** Carry per-request inputs from MHD's worker thread to the user handler. Lazily-cache derived data (path pieces, parsed args, basic-auth credentials, client cert fields).

**Implementation:** PIMPL via `std::unique_ptr<http_request_impl>`. The impl is **arena-allocated** from a `std::pmr::monotonic_buffer_resource` that lives on the connection (one arena per MHD connection, reset between requests on the same keep-alive connection). The arena also backs the impl's owned strings and lazy-cache containers where practical, eliminating per-request `malloc` on the hot path.

**Interfaces:**
- Exposes (from PRD §3.6):
  - `get_path()`, `get_method()`, `get_version()`, `get_content()`, `get_querystring()` returning `string_view`
  - `get_headers()`, `get_footers()`, `get_cookies()`, `get_args()`, `get_path_pieces()`, `get_files()` returning `const ContainerType&`
  - `get_header(key)`, `get_cookie(key)`, `get_footer(key)`, `get_arg(key)`, `get_arg_flat(key)` returning `string_view` (empty on miss; never insert)
  - `get_user()`, `get_pass()`, `get_digested_user()` returning `string_view` (empty when basic/digest auth disabled at build)
  - `has_tls_session()`, `has_client_certificate()`, `get_client_cert_dn()`, `get_client_cert_issuer_dn()`, `get_client_cert_cn()`, `get_client_cert_fingerprint_sha256()`, `is_client_cert_verified()`, `get_client_cert_not_before()`, `get_client_cert_not_after()` (all returning sentinels when GnuTLS disabled)
  - `check_digest_auth(...)` family
  - `get_requestor()`, `get_requestor_port()`
- All getters are `const`. Lazy caches use `mutable` (or unique_ptr indirection); the const-correctness NFR's exemption for daemon-driving methods does not apply to request — every request getter is logically const.
- Move-only (preserves identity; rules out shared ownership). PRD §3.6 out-of-scope: not changing the move-only identity.

**Key design notes:**
- The arena allocator is plumbed through `webserver_impl` → connection state → `http_request` constructor. The user does not see it; it is an internal optimization.
- Containers returned by `get_*()` reference impl-owned storage; the request must outlive any view derived from it. Documented as a lifetime contract.
- `gnutls_session_t` (raw GnuTLS handle) is not exposed publicly. Users wanting custom TLS introspection use the high-level `get_client_cert_*` accessors. The handle remains accessible via friend access from internal code.

**Related requirements:** PRD-HDR-REQ-001..004, PRD-FLG-REQ-001..002, PRD-REQ-REQ-001, PRD-RSP-REQ-* (for the response side of the request/response cycle).

---
