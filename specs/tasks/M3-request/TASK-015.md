### TASK-015: `http_request_impl` skeleton (PIMPL split, structural only)

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** `http_request` / `http_request_impl`
**Estimate:** M

**Goal:**
Move `http_request`'s backend-coupled members (`MHD_Connection*`, raw GnuTLS handle, computed caches) into `details/http_request_impl.hpp` behind a `std::unique_ptr<http_request_impl>`. No API rename yet.

**Action Items:**
- [ ] Create `src/httpserver/details/http_request_impl.hpp` (gated `HTTPSERVER_COMPILATION` only).
- [ ] Move all backend-coupled state into the impl struct: `MHD_Connection* conn_`, `gnutls_session_t tls_session_`, parsed-args cache, headers cache, etc.
- [ ] Public `http_request.hpp` declares `std::unique_ptr<http_request_impl> impl_;` and forward-declares the impl class.
- [ ] Implement existing public methods as forwarders to `impl_->method()`.
- [ ] Move `<microhttpd.h>`, `<gnutls/gnutls.h>` includes from public `http_request.hpp` into `http_request_impl.hpp` and `http_request.cpp`.

**Dependencies:**
- Blocked by: TASK-002, TASK-014
- Blocks: TASK-016, TASK-017, TASK-018, TASK-019, TASK-020

**Acceptance Criteria:**
- `grep -E '#include\s+<(microhttpd|gnutls/gnutls)\.h>' src/httpserver/http_request.hpp` returns nothing.
- All v1 request-side tests pass.
- `sizeof(http_request)` reduces to a single pointer plus any non-impl members.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDR-REQ-001..004
**Related Decisions:** DR-003b, §4.2

**Status:** Not Started
