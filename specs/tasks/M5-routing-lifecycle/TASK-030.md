### TASK-030: `_handler` suffix renames + `explicit` constructor

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** `webserver` setters and constructor
**Estimate:** S

**Goal:**
Distinguish function-handler setters from object-resource setters by suffix, and prevent surprising implicit conversions to `webserver`.

**Action Items:**
- [ ] Rename setters on `create_webserver` (or `webserver`, wherever they live):
  - `not_found_resource` → `not_found_handler`
  - `method_not_allowed_resource` → `method_not_allowed_handler`
  - `internal_error_resource` → `internal_error_handler`
- [ ] These setters take a function-shaped handler (`std::function<http_response(const http_request&)>`), matching the `_handler` suffix convention.
- [ ] Mark `webserver(const create_webserver&)` constructor `explicit`; remove the `// NOLINT(runtime/explicit)` if present.
- [ ] Remove old `_resource` names entirely (no compatibility shim).

**Dependencies:**
- Blocked by: TASK-014
- Blocks: TASK-031

**Acceptance Criteria:**
- A test verifies implicit conversion `webserver w = some_create_webserver;` no longer compiles; explicit `webserver w(some_create_webserver);` does.
- `grep -E '(not_found|method_not_allowed|internal_error)_resource' src/httpserver/*.hpp` returns no results.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-NAM-REQ-003, PRD-NAM-REQ-004
**Related Decisions:** §3.7, §4.1

**Status:** Not Started
