### 4.9 `create_webserver` (builder)

**Responsibility:** Configuration builder for `webserver`.

**Implementation:** Single-class builder, ~half the v1 line count. Each paired `foo()/no_foo()` collapses to `foo(bool = true)` (PRD-CFG-REQ-001). All `#define` constants (`DEFAULT_WS_PORT`, `DEFAULT_WS_TIMEOUT`, `NOT_FOUND_ERROR`) move to `constexpr` in `httpserver::constants` (PRD-CFG-REQ-002). Out-of-range setters throw `std::invalid_argument` (PRD-CFG-REQ-003).

The builder remains non-PIMPL (it's a pure value carrier; PIMPL would buy nothing).

**`auth_handler_ptr` shape (TASK-054).** The centralised authentication
callback is `std::function<std::optional<http_response>(const http_request&)>`.
Returning `std::nullopt` allows the request through; returning an
engaged optional short-circuits the before_handler chain with that
response (the dispatcher moves it into `mr->response`). The earlier v2
work-in-progress shape returned `std::shared_ptr<http_response>`; that
shape remains available for one transitional build via the
`httpserver::compat::auth_handler_v1_ptr` typedef alias and a
`[[deprecated]]` setter overload (`auth_handler(compat::auth_handler_v1_ptr)`),
both of which wrap the legacy callable via `compat::adapt_legacy_auth`
into the canonical `auth_handler_ptr` shape and emit a deprecation
diagnostic at the call site. The compat alias and overload will be
removed after the next release. Rationale: completes the
PRD-RSP-REQ-007 value-typed response cutover (DR-009) onto the auth
path, removing the per-authenticated-request control-block allocation.

**Related requirements:** PRD-CFG-REQ-001..004, PRD-RSP-REQ-007.

---
