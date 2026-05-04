### TASK-017: `http_request` container getters return `const&`

**Milestone:** M3 - Webserver internal & Request Refactor
**Component:** `http_request`
**Estimate:** M

**Goal:**
Stop copying maps/vectors out of `http_request` on every getter call.

**Action Items:**
- [ ] Change return types of `get_args`, `get_path_pieces`, `get_files`, `get_headers`, `get_footers`, `get_cookies` from by-value to `const ContainerType&`.
- [ ] Mark each getter `const`.
- [ ] If a v1 caller relied on copy semantics (modifying the returned value), update it to copy explicitly at the call site.
- [ ] Document in the header that the returned reference is valid until the request object is destroyed (typically until handler return).

**Dependencies:**
- Blocked by: TASK-015
- Blocks: TASK-039

**Acceptance Criteria:**
- `static_assert(std::is_lvalue_reference_v<decltype(std::declval<const http_request&>().get_headers())>);`
- Microbenchmark of `req.get_headers()` shows ≥10× reduction vs v1 (PRD §3.6 acceptance — measured in TASK-039).
- All callers in test/ migrated.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-REQ-REQ-001
**Related Decisions:** §4.2

**Status:** Not Started
