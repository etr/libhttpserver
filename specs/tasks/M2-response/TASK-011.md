### TASK-011: `http_response` const-correct accessors

**Milestone:** M2 - Response Refactor
**Component:** `http_response`
**Estimate:** M

**Goal:**
Make read accessors callable on `const http_response&`, returning views without inserting on miss.

**Action Items:**
- [x] `std::string_view get_header(std::string_view key) const;` returns empty view on miss; does NOT insert.
- [x] Same for `get_footer(std::string_view) const;` and `get_cookie(std::string_view) const;`.
- [x] `const header_map& get_headers() const noexcept;` (and `get_footers`, `get_cookies`).
- [x] `int get_status() const noexcept;`
- [x] `body_kind kind() const noexcept;`
- [x] Remove any v1 accessor that inserted on miss (e.g., `headers[key]` patterns).
- [x] Audit `string_view` returns: the storage must outlive the view. Document lifetime contract on each accessor (views invalidated by mutation of the response, e.g., `with_header` may rehash the map).

**Dependencies:**
- Blocked by: TASK-009
- Blocks: TASK-013

**Acceptance Criteria:**
- `void f(const http_response& r) { auto v = r.get_header("X-Foo"); }` compiles.
- After `r.get_header("missing");` the response's headers map size is unchanged (no insert-on-miss).
- Unit test reads back a header set via `with_header` from a `const&` reference.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-002, PRD-RSP-REQ-003
**Related Decisions:** §2.2 (const correctness), §4.3

**Status:** Done
