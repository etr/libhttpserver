### TASK-049: Fire `handler_exception`; wire `internal_error_handler` alias

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** S

**Goal:**
Convert the existing DR-009 §5.2 dispatch-exception path into a hookable chain. The `internal_error_handler` setter becomes the last-position alias hook so all existing behavior is preserved.

**Action Items:**
- [x] Fire `handler_exception` from each catch arm in `webserver_impl::dispatch_resource_handler` (`webserver.cpp:2392` and `webserver.cpp:2400`). Context: `handler_exception_ctx { const http_request& req; std::exception_ptr eptr; std::string_view message; }`. `message` is `e.what()` for the `std::exception` arm and the literal `"unknown exception"` for the `catch (...)` arm.
- [x] Chain semantics: hooks run in registration order; the first to return `hook_action::respond_with(r)` wins and the response is stashed into `mr->response_`. If no hook short-circuits, fall through to `run_internal_error_handler_safely(mr, message)` — preserving today's hardcoded fallback (empty-body 500 if the user `internal_error_handler` itself throws).
- [x] **Alias: `internal_error_handler(fn)`.** Internally register a `handler_exception` hook at LAST position that builds the same response `internal_error_handler` builds today and returns `hook_action::respond_with(...)`. Re-registration replaces the existing alias hook rather than chaining — single-slot semantics preserved for the alias surface (a user can still add their own `handler_exception` hooks before it via `add_hook`).
- [x] A throwing `handler_exception` hook is caught and the chain continues to the next hook (rather than short-circuiting to the hardcoded fallback immediately) — this is the one phase where exception-in-exception-handler does NOT abort the chain, because the whole point of the chain is exception recovery.
- [x] Doxygen on `internal_error_handler` setter notes it is an alias for a last-position `handler_exception` hook and references DR-012.

**Dependencies:**
- Blocked by: TASK-045, TASK-031 (existing error contract)
- Blocks: TASK-052

**Acceptance Criteria:**
- New integ test `hooks_handler_exception_chain`: registers two `handler_exception` hooks (A returns `pass()`, B returns `respond_with(418_response)`) and an `internal_error_handler` alias (C). A handler that throws `std::runtime_error("boom")` triggers A → B; B wins; C is never called.
- New integ test `hooks_handler_exception_user_handler_throws_continues_chain`: A throws; B is still invoked; B's response is queued.
- All existing DR-009 §5.2 tests pass unchanged.
- The hardcoded empty-body-500 fallback still fires when every hook in the chain (including the `internal_error_handler` alias) either throws or returns `pass()`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-002, PRD-HOOK-REQ-003, PRD-HOOK-REQ-005, PRD-HOOK-REQ-009
**Related Decisions:** DR-009, DR-012, §4.10, §5.2

**Status:** Done
