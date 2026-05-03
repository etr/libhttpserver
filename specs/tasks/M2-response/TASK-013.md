### TASK-013: Remove `*_response` subclasses and dispatch virtuals from public API

**Milestone:** M2 - Response Refactor
**Component:** `http_response`
**Estimate:** M

**Goal:**
Delete the public-facing response subclasses and the `get_raw_response`/`decorate_response`/`enqueue_response` virtuals so the new factory-based surface is the only way to build a response.

**Action Items:**
- [ ] Remove `src/httpserver/string_response.hpp`, `file_response.hpp`, `iovec_response.hpp`, `pipe_response.hpp`, `deferred_response.hpp`, `empty_response.hpp`, `basic_auth_fail_response.hpp`, `digest_auth_fail_response.hpp` from the installed set.
- [ ] Delete those classes' source files (or move any salvageable logic into `detail/body.hpp`).
- [ ] Remove the public virtual methods `get_raw_response`, `decorate_response`, `enqueue_response` from `http_response.hpp`.
- [ ] Update `<httpserver.hpp>` umbrella to drop the removed includes.
- [ ] Internal dispatch path (in `webserver.cpp` or `http_response.cpp`) calls `body_->materialize(...)` instead of the removed virtuals.
- [ ] Add `final` to `http_response` (deferred from TASK-009 because the v1 subclasses still inherited at that point — see TASK-009 plan OQ-1). Per PRD §3.5 the class must be sealed.

**Dependencies:**
- Blocked by: TASK-009, TASK-010, TASK-011, TASK-012
- Blocks: None

**Acceptance Criteria:**
- `grep -E 'class\s+\w+_response\s*:' src/httpserver/*.hpp` returns no public results (PRD §3.5 acceptance).
- `grep -E 'get_raw_response|decorate_response|enqueue_response' src/httpserver/*.hpp` returns no results.
- `static_assert(std::is_final_v<httpserver::http_response>);` (deferred AC from TASK-009 — PRD §3.5 sealed value type).
- Existing tests that constructed `string_response` etc. directly are migrated to factories (or removed if they were testing private details).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-006, PRD-HDR-REQ-005
**Related Decisions:** §4.3, §4.8

**Status:** Not Started
