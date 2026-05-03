### TASK-024: `register_path` and `register_prefix` (replace `bool family`)

**Milestone:** M4 - Handler & Resource Model
**Component:** `webserver` registration API
**Estimate:** M

**Goal:**
Make prefix-vs-exact matching a named API choice rather than a positional `bool` flag.

**Action Items:**
- [ ] Add `register_path(const std::string& path, std::unique_ptr<http_resource>);` and `(..., std::shared_ptr<http_resource>);` — exact-match registration.
- [ ] Add `register_prefix(const std::string& path, std::unique_ptr<http_resource>);` and `(..., std::shared_ptr<http_resource>);` — prefix-match registration.
- [ ] Document the distinction: `register_path("/users/{id}")` matches only the parameterized exact form; `register_prefix("/static/")` matches `/static/anything/here`.
- [ ] `register_resource` (TASK-023) becomes either an alias for `register_path` or is kept as the umbrella entry point that internally calls one of the two — pick one and document.
- [ ] Remove the `bool family` parameter from any surviving overload.
- [ ] Update `unregister_resource(path)` to handle both registration kinds (or split into `unregister_path`/`unregister_prefix`).

**Dependencies:**
- Blocked by: TASK-023
- Blocks: TASK-027

**Acceptance Criteria:**
- `grep -E 'register_resource\([^)]+,\s*bool\s' src/httpserver/*.hpp` returns no results.
- A test registers a prefix route and verifies a longer path matches; same test verifies an exact-path registration does NOT match a longer path.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-004
**Related Decisions:** §4.7

**Status:** Not Started
