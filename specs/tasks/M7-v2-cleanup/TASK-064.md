### TASK-064: Structured cookie type

**Milestone:** M7 - v2 Cleanup
**Component:** `http_response::with_cookie`, request cookie accessors
**Estimate:** M

**Goal:**
Replace the string-blob cookie surface on `http_response` with a structured `httpserver::cookie` value type carrying `name`, `value`, `domain`, `path`, `expires`, `max_age`, `secure`, `http_only`, `same_site`. The follow-up was explicitly deferred at `src/httpserver/http_response.hpp:304-313`.

**Action Items:**
- [ ] Design the `httpserver::cookie` value type in a new public header `src/httpserver/cookie.hpp`. Default-construct empty; provide fluent `with_*` setters mirroring `http_response`'s style. Include enum `same_site_mode { unset, strict, lax, none }`.
- [ ] Add `http_response::with_cookie(cookie)` and `http_response::with_cookie(std::string name, std::string value)` overloads. Internally render to the `Set-Cookie` header per RFC 6265 §4.1.
- [ ] Provide `http_request::get_cookies()` returning a structured view (parsed once, cached on the request impl per TASK-016 arena pattern).
- [ ] Document migration: legacy string-blob path remains as a `[[deprecated]]` thin shim for one transitional release.
- [ ] Add a unit test pinning round-trip parsing/rendering against RFC 6265 examples.

**Dependencies:**
- Blocked by: TASK-016 (arena), TASK-009 (http_response value type)
- Blocks: None

**Acceptance Criteria:**
- `http_response::ok().with_cookie(cookie{}.with_name("sid").with_secure(true).with_same_site(same_site_mode::strict))` compiles and emits a single, well-formed `Set-Cookie` header.
- `http_request::get_cookies()` returns `const std::vector<cookie>&` (or `span<const cookie>`) for read-only access; never allocates after the first call.
- RFC 6265 round-trip examples pass.
- Deprecated string-blob path still compiles with a `[[deprecated]]` warning.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-004 (fluent return), PRD §2 API minimalism
**Related Decisions:** None new (RFC 6265)

**Status:** Backlog
