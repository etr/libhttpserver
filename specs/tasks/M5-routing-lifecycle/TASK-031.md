### TASK-031: Handler error-propagation contract (DR-009)

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Dispatch path
**Estimate:** M

**Goal:**
Implement the 6-point error-propagation contract from §5.2 / DR-009 in the dispatch path so any uncaught exception lands at the configured `internal_error_handler` with documented behavior.

**Action Items:**
- [ ] Wrap handler invocation in dispatch with `try { ... } catch (const std::exception& e) { ... } catch (...) { ... }`.
- [ ] On `std::exception`: log via `error_logger` (whatever callback the user wired), invoke `internal_error_handler` with `e.what()`, send the resulting response (default 500 if no handler set).
- [ ] On non-`std::exception`: same path but with message `"unknown exception"`.
- [ ] If `internal_error_handler` itself throws: log generically, send hardcoded 500 with empty body.
- [ ] `feature_unavailable` is a `std::runtime_error`; no special status mapping (just lands as a 500 like any other exception).
- [ ] Document the contract in `webserver.hpp` Doxygen comments (full README pass in M6).

**Dependencies:**
- Blocked by: TASK-027, TASK-030
- Blocks: TASK-032, TASK-036, TASK-041, TASK-043

**Acceptance Criteria:**
- A handler that throws `std::runtime_error("boom")` produces a 500 response whose body / log message contains "boom" (when default handler is used) or whatever `internal_error_handler` produced.
- A handler that throws an `int` produces a 500 with the documented "unknown exception" message.
- An `internal_error_handler` that itself throws produces an empty-body 500 (test verifies the body is empty and status is 500).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-FLG-REQ-002 (sentinel/throw behavior)
**Related Decisions:** DR-009, §5.2, AR-007

**Status:** Not Started
