### 4.9 `create_webserver` (builder)

**Responsibility:** Configuration builder for `webserver`.

**Implementation:** Single-class builder, ~half the v1 line count. Each paired `foo()/no_foo()` collapses to `foo(bool = true)` (PRD-CFG-REQ-001). All `#define` constants (`DEFAULT_WS_PORT`, `DEFAULT_WS_TIMEOUT`, `NOT_FOUND_ERROR`) move to `constexpr` in `httpserver::constants` (PRD-CFG-REQ-002). Out-of-range setters throw `std::invalid_argument` (PRD-CFG-REQ-003).

The builder remains non-PIMPL (it's a pure value carrier; PIMPL would buy nothing).

**`auth_handler_ptr` shape (TASK-054).** The centralised authentication
callback is `std::function<std::optional<http_response>(const http_request&)>`.
Returning `std::nullopt` allows the request through; returning an
engaged optional short-circuits the before_handler chain with that
response (the dispatcher moves it into `conn->response`). The earlier v2
work-in-progress shape returned `std::shared_ptr<http_response>`; that
shape remains available for one transitional build via the
`httpserver::compat::auth_handler_v1_ptr` typedef alias and a
`[[deprecated]]` setter overload (`auth_handler(compat::auth_handler_v1_ptr)`),
both of which wrap the legacy callable via `compat::adapt_legacy_auth`
into the canonical `auth_handler_ptr` shape and emit a deprecation
diagnostic at the call site. The compat alias and overload are scheduled
for removal in v2.1. Rationale: completes the
PRD-RSP-REQ-007 value-typed response cutover (DR-009) onto the auth
path, removing the per-authenticated-request control-block allocation.

**`expose_exception_messages` (TASK-055 / DR-009 Revision 1).**
Security-opt-in builder setter. Default `false` makes the
no-handler-set 500 path return the fixed string `"Internal Server Error"`
as the response body — `e.what()` is logged via `log_error` but never
crosses a process boundary on the wire (CWE-209). Setting `true`
restores the v1 verbose body that surfaces the message. Intended
for development only; must not be set in production.

**`expose_credentials_in_logs` (TASK-057).** Security-opt-in builder
setter that propagates to every `http_request` the webserver dispatches.
Default `false` suppresses credential surfaces in `http_request::operator<<`
(Basic-auth `pass`, `Authorization`/`Proxy-Authorization` headers and
footers, cookie values) by emitting the fixed token `<redacted>`
(CWE-312 / CWE-532). Setting `true` restores the v1 verbose form for
local development; the flag must not be set in production. Shape
mirrors `expose_exception_messages` (see §5.2.1 for the cross-cutting
contract).

**Related requirements:** PRD-CFG-REQ-001..004, PRD-RSP-REQ-007.

---
