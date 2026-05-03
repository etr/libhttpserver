### TASK-018: `http_request` single-key getters return `string_view`, all const

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** `http_request`
**Estimate:** M

**Goal:**
Make per-key lookups allocation-free and callable on `const http_request&`, with empty result on miss instead of insertion.

**Action Items:**
- [ ] `string_view get_header(string_view key) const;` — empty on miss; never inserts.
- [ ] Same for `get_cookie`, `get_footer`, `get_arg`, `get_arg_flat`.
- [ ] `string_view get_path() const noexcept;`, `get_method() const noexcept;`, `get_version() const noexcept;`, `get_content() const noexcept;`, `get_querystring() const noexcept;`.
- [ ] Replace any v1 path that modified internal state from a getter (e.g., lazy parse caches) to use `mutable` storage on the impl with a one-time-fill pattern, keeping the public method `const`.
- [ ] Document lifetime: the view is valid for the lifetime of the request object (which is the lifetime of the handler invocation).

**Dependencies:**
- Blocked by: TASK-015, TASK-016
- Blocks: TASK-039

**Acceptance Criteria:**
- `void f(const http_request& r) { auto v = r.get_header("X-Foo"); }` compiles.
- Calling `r.get_header("missing")` does not increase the headers map size.
- All getters introspectable via `static_assert(std::is_invocable_v<decltype(&http_request::get_header), const http_request&, std::string_view>);`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 const-correctness NFR, PRD-REQ-REQ-001
**Related Decisions:** §2.2, §4.2

**Status:** Not Started
