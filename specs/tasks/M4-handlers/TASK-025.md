### TASK-025: Lambda handler entry points `on_*`

**Milestone:** M4 - Handler & Resource Model
**Component:** `webserver` registration API
**Estimate:** L

**Goal:**
Add the lambda-first handler model that lets a stateless endpoint be registered without subclassing.

**Action Items:**
- [ ] Add `webserver::on_get(const std::string& path, std::function<http_response(const http_request&)> handler);`.
- [ ] Same for `on_post`, `on_put`, `on_delete`, `on_patch`, `on_options`, `on_head`.
- [ ] Internally, each `on_*` builds a `route_entry` whose `method_set` carries exactly that one method, then registers it in the appropriate route-table tier (hash for exact, radix for parameterized).
- [ ] Multiple `on_*` calls on the same path compose: each call adds the corresponding method bit; conflicting handlers on the same (method, path) pair throw `std::invalid_argument`.
- [ ] Make sure the variant in `route_entry` can hold both `std::function<http_response(const http_request&)>` (lambda) and `std::shared_ptr<http_resource>` (class) — see §4.7.
- [ ] Add a parallel `on_get` (etc.) that takes `(method_set methods, ...)` if useful, or defer that to TASK-026's generic `route()`.

**Dependencies:**
- Blocked by: TASK-005, TASK-009, TASK-014
- Blocks: TASK-026, TASK-027, TASK-036, TASK-040

**Acceptance Criteria:**
- A "hello world" example using `ws.on_get("/", [](auto&){ return http_response::string("hi"); });` compiles, runs, returns 200 "hi" on GET / (PRD §3.4 acceptance).
- Registering `on_get` and `on_post` on the same path serves both methods from the same route entry.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-001, PRD-HDL-REQ-002
**Related Decisions:** DR-004, §4.7

**Status:** Not Started
