### TASK-012: `http_response` fluent `with_*` setters

**Milestone:** M2 - Response Refactor
**Component:** `http_response`
**Estimate:** S

**Goal:**
Make `with_header` / `with_footer` / `with_cookie` / `with_status` return `http_response&` so factory chains work.

**Action Items:**
- [x] `http_response& with_header(std::string key, std::string value) &;`
- [x] `http_response&& with_header(std::string key, std::string value) &&;` (rvalue overload to keep `http_response::string("hi").with_header(...)` zero-copy).
- [x] Same pattern for `with_footer`, `with_cookie`, `with_status(int code)`.
- [x] Cookie API takes a structured cookie type (name, value, attrs) or string-as-Set-Cookie; pick one and document. (Decision: keep v1 string-pair `(name, value)`; structured cookie type deferred to a follow-up task. Documented on `with_cookie` Doxygen.)
- [x] Update v1 callers: `r.with_header(...)` chains now compile; previous `void`-returning calls still work (statement form is fine) but enable the fluent style.

**Dependencies:**
- Blocked by: TASK-009
- Blocks: TASK-013

**Acceptance Criteria:**
- `auto r = http_response::string("hi").with_header("X-Foo", "bar").with_status(201);` compiles and produces the expected response (PRD §3.5 acceptance).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-004
**Related Decisions:** §4.3

**Status:** Done
