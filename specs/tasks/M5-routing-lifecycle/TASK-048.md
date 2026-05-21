### TASK-048: Fire `route_resolved` and `before_handler`; wire 404/405/auth aliases

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** M

**Goal:**
Wire the routing-boundary observation phase and the pre-handler short-circuit phase. Convert three of the v1-derived single-slot setters into documented aliases that internally register at one of these phases.

**Action Items:**
- [ ] Fire `route_resolved` from the end of `webserver_impl::resolve_resource_for_request` (`webserver.cpp:2296`), and again from the miss branch in `finalize_answer` (`webserver.cpp:2446`). Context: `route_resolved_ctx { const http_request& req; std::optional<route_descriptor> matched; }`. `route_descriptor` is a small libhttpserver type carrying `{path_template, http_method method_or_set, bool is_prefix}` — no MHD or internal route_entry leakage.
- [ ] Fire `before_handler` at the start of `webserver_impl::dispatch_resource_handler` (`webserver.cpp:2363`), AFTER the existing `mr->pp` destroy step and BEFORE the `is_allowed` check. Context: `before_handler_ctx { http_request& req; const http_resource& resource; http_method method; }`.
- [ ] Short-circuit handling for `before_handler`: returning `hook_action::respond_with(r)` skips the resource invocation and the method-allowed check; the response goes straight to materialization.
- [ ] **Alias: `auth_handler(fn)`.** Internally register a `before_handler` hook that calls `fn(req)`; if the returned `http_response` is non-empty (in the same sense as today's `auth_handler` returning a non-null shared_ptr), return `hook_action::respond_with(std::move(*resp))`. The existing `auth_handler_ptr` storage on `webserver` becomes a thin wrapper that calls `add_hook` at construction time. The `should_skip_auth` / `auth_skip_paths` logic is preserved as the hook's first check.
- [ ] **Alias: `method_not_allowed_handler(fn)`.** Internally register a `before_handler` hook (registered AFTER any user-supplied auth hooks, by document) that runs `is_allowed(method)` — if false, builds the same 405-with-Allow-header response the dispatch path builds today and returns `hook_action::respond_with(...)`. The current inline branch in `dispatch_resource_handler` becomes the default content of this hook.
- [ ] **Alias: `not_found_handler(fn)`.** Internally register a `route_resolved` hook that, when `matched == std::nullopt`, builds the 404 page and stashes it into `mr->response_`. The current `not_found_page(mr)` call in `finalize_answer` becomes the body of the default hook (still installed at webserver construction when no user `not_found_handler` is set, so the default 404 behavior is preserved).
- [ ] Doxygen on each alias setter explicitly states: "This is an alias. Calling it registers a hook at `<phase>`. Equivalent to `ws.add_hook(hook_phase::<phase>, ...)`."
- [ ] Per-route hooks at `before_handler` are NOT yet wired (TASK-051) — server-wide only at this task.

**Dependencies:**
- Blocked by: TASK-045, TASK-027 (route table — for `route_descriptor` shape), TASK-031 (existing error contract — the alias hooks must still flow through it)
- Blocks: TASK-051, TASK-052

**Acceptance Criteria:**
- New integ test `hooks_route_resolved_miss_and_hit`: registers two `route_resolved` hooks; on a hit, both observe `matched != std::nullopt`; on a miss, both observe `matched == std::nullopt`.
- New integ test `hooks_before_handler_short_circuit_replaces_dispatch`: a `before_handler` hook returning `hook_action::respond_with(r)` is observed on the wire; the resource's `render_get` is never called.
- The existing tests for `auth_handler`, `not_found_handler`, and `method_not_allowed_handler` still pass unchanged (the aliases are behavior-preserving).
- A test verifies the alias is observably an alias: registering `auth_handler(fn)` then querying the `before_handler` hook count reports +1.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-002, PRD-HOOK-REQ-003, PRD-HOOK-REQ-005, PRD-HOOK-REQ-009
**Related Decisions:** DR-012, §4.10

**Status:** Not Started
