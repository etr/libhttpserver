### TASK-022: Snake_case `render_*` overrides on `http_resource`

**Milestone:** M4 - Handler & Resource Model
**Component:** `http_resource`
**Estimate:** M

**Goal:**
Rename `render_GET` / `render_POST` / etc. to `render_get` / `render_post` / etc. so the public API obeys the snake_case rule.

**Action Items:**
- [ ] Rename virtual overrides:
  - `render_GET` → `render_get`
  - `render_POST` → `render_post`
  - `render_PUT` → `render_put`
  - `render_DELETE` → `render_delete`
  - `render_PATCH` → `render_patch`
  - `render_OPTIONS` → `render_options`
  - `render_HEAD` → `render_head`
  - `render_CONNECT` → `render_connect`
  - `render_TRACE` → `render_trace`
- [ ] Default `render(...)` fallback signature unchanged.
- [ ] Update return type to `http_response` by value (was a pointer / shared_ptr in v1) — coupled with TASK-036's full handler-return refactor.
- [ ] Update all examples and tests to use the new names.
- [ ] Remove the old camelCase names entirely (no compatibility shim — v2.0 is a clean break).

**Dependencies:**
- Blocked by: TASK-021
- Blocks: TASK-036

**Acceptance Criteria:**
- `grep -E 'render_[A-Z]' src/httpserver/*.hpp` returns no results.
- A subclass overriding `render_get` is invoked correctly for an HTTP GET (existing routing tests cover this with renamed expectations).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-NAM-REQ-001
**Related Decisions:** §3.7, §4.4

**Status:** Not Started
