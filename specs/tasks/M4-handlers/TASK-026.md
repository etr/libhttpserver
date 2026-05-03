### TASK-026: Generic `webserver::route(method, path, handler)`

**Milestone:** M4 - Handler & Resource Model
**Component:** `webserver` registration API
**Estimate:** M

**Goal:**
Provide the table-driven escape hatch for registering handlers when the HTTP method is a runtime value.

**Action Items:**
- [ ] Add `webserver::route(http_method m, const std::string& path, std::function<http_response(const http_request&)> handler);`.
- [ ] Implementation dispatches to the same internal registration path used by `on_*`.
- [ ] Document the call-site convention: `route()` is the escape hatch; `on_*` is preferred when the method is known statically.
- [ ] Add `webserver::route(method_set methods, const std::string& path, handler)` if a single handler should serve multiple methods (e.g., GET and HEAD).

**Dependencies:**
- Blocked by: TASK-005, TASK-025
- Blocks: TASK-027

**Acceptance Criteria:**
- A test loads `[(GET, "/a"), (POST, "/b")]` from a vector at runtime and registers each via `route()`, then verifies both serve correctly.
- `webserver::route(method_set{}.set(http_method::get).set(http_method::head), "/c", h);` compiles and serves both methods.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-006
**Related Decisions:** §4.7, OQ-003 resolution

**Status:** Not Started
