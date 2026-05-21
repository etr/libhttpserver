### TASK-051: Per-route hooks (`http_resource::add_hook`)

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** M

**Goal:**
Add a per-resource scope to the hook bus so a hook can be attached to one route instead of every request. Required to give #273-style early-413 a per-route flavor (different size caps per endpoint), to put per-route timing in `response_sent`, etc.

**Action Items:**
- [ ] Add `http_resource::add_hook(hook_phase, ...)` overloads — but ONLY for phases that fire after route resolution: `before_handler`, `handler_exception`, `after_handler`, `response_sent`, `request_completed`. Passing any earlier phase throws `std::invalid_argument` with a message naming the phase and the constraint.
- [ ] Add per-resource storage on `http_resource`: `std::array<std::vector<std::function<...>>, hook_phase::count_>` (most slots unused per the constraint above, but the array shape simplifies indexed access) + `std::array<std::atomic<bool>, hook_phase::count_> any_hooks_` + `std::shared_mutex hook_table_mutex_`. Memory footprint is a fixed-size array of empty vectors; revisit if TASK-039's `sizeof(http_resource)` budget is tight (a single `std::vector` of `std::pair<hook_phase, function>` is the fallback if so).
- [ ] In `webserver_impl::dispatch_resource_handler`, fire the per-route hook chain at each of the five applicable phases AFTER the server-wide chain at that phase, BEFORE checking for a server-wide short-circuit's result. (If server-wide short-circuited, do NOT run per-route — the response is already fixed.)
- [ ] In `webserver_impl::request_completed`, locate the resource that handled the request (carried on `modded_request` as a `weak_ptr<http_resource>` populated at dispatch time) and fire its `request_completed` per-route chain after the server-wide one. If the resource was unregistered between dispatch and completion, the `lock()` returns null and the per-route chain is skipped.
- [ ] Lock order: `route_table_mutex_` (already held by dispatch) → resource's `hook_table_mutex_` → server-wide `hook_table_mutex_`. Documented in `specs/architecture/05-cross-cutting.md` §5.6 and verified by TSan.
- [ ] `hook_handle` returned by `http_resource::add_hook` carries the same RAII semantics as the server-wide handle — destruction removes the registration. The handle holds a `weak_ptr<http_resource>`; if the resource is destroyed before the handle, `remove()` is a no-op.

**Dependencies:**
- Blocked by: TASK-045, TASK-048, TASK-049, TASK-050
- Blocks: TASK-052

**Acceptance Criteria:**
- New integ test `hooks_per_route_order`: registers a server-wide `response_sent` hook A and a per-route `response_sent` hook B on resource R. A request to R fires A → B in that order. A request to another resource fires only A.
- New integ test `hooks_per_route_early_413_per_endpoint`: per-route `before_handler` hook on `/upload-small` rejects bodies > 1KB with 413; on `/upload-large` rejects > 1GB. Different policies, no global side-effects.
- New integ test `hooks_per_route_invalid_phase_throws`: `r.add_hook(hook_phase::request_received, ...)` throws `std::invalid_argument`.
- New integ test `hooks_per_route_resource_destroyed_first`: a `hook_handle` whose resource was destroyed has `remove()` as a no-op (no crash, no UAF — verified under ASan).
- TSan-clean under the existing tsan CI matrix entry; the route_table → resource → server-wide lock order is exercised by `hooks_per_route_concurrent_registration` (registration on resource R from inside a handler on resource Q).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-002, PRD-HOOK-REQ-003, PRD-HOOK-REQ-004, PRD-HOOK-REQ-006, PRD-HOOK-REQ-007
**Related Decisions:** DR-012, §4.10, §5.6

**Status:** Not Started
