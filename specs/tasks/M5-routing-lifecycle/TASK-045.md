### TASK-045: Hook bus skeleton (`hook_phase`, `hook_action`, `hook_handle`, `webserver::add_hook`)

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** L

**Goal:**
Land the public types and per-phase storage for the lifecycle hook bus without firing any hooks yet. After this task the API surface compiles and a hook can be registered + removed, but no phase actually fires; subsequent tasks (TASK-046 .. TASK-051) wire individual phases.

**Action Items:**
- [x] New public header `src/httpserver/hook_phase.hpp`: define `enum class hook_phase` with the eleven phases plus `count_` sentinel (per §4.10).
- [x] New public header `src/httpserver/hook_action.hpp`: `class hook_action` with `pass()`, `respond_with(http_response)`, `is_pass() const noexcept`, `http_response&& take_response() &&`. Implementation over `std::optional<http_response>`.
- [x] New public header `src/httpserver/hook_handle.hpp`: move-only RAII type holding a non-owning reference into the per-phase storage. Destructor calls `remove()` unless previously `detach()`-ed.
- [x] New public header `src/httpserver/hook_context.hpp`: phase-specific context structs (`accept_ctx`, `request_received_ctx`, `body_chunk_ctx`, `route_resolved_ctx`, `before_handler_ctx`, `handler_exception_ctx`, `after_handler_ctx`, `response_sent_ctx`, `request_completed_ctx`, `connection_open_ctx`, `connection_close_ctx`, `peer_address`). All libhttpserver-defined; never expose MHD types (PRD-HDR-REQ-001).
- [x] `webserver_impl` private state in `src/httpserver/detail/webserver_impl.hpp`: one `std::vector<std::function<...>>` per phase + `std::array<std::atomic<bool>, hook_phase::count_> any_hooks_` + a single `std::shared_mutex hook_table_mutex_` covering all phase vectors.
- [x] `webserver::add_hook(hook_phase, ...)` — eleven overloads, one per phase. Each acquires `hook_table_mutex_` (unique_lock), pushes the callable, sets `any_hooks_[phase] = true` (memory_order_release), returns a `hook_handle` carrying back-references for removal.
- [x] `hook_handle::remove()` re-takes the `hook_table_mutex_` (unique_lock), erases the entry, clears `any_hooks_[phase]` if the vector is now empty (memory_order_release). Idempotent.
- [x] Add `hook_phase.hpp`, `hook_action.hpp`, `hook_handle.hpp`, `hook_context.hpp` to the umbrella include in `src/httpserver.hpp` and to install rules in `src/httpserver/Makefile.am`.
- [x] Extend the public-header hygiene CI test (TASK-007) to cover the four new headers — they must carry the `_HTTPSERVER_HPP_INSIDE_` / `HTTPSERVER_COMPILATION` guard pair.

**Dependencies:**
- Blocked by: TASK-014 (webserver_impl), TASK-009 (http_response by value).
- Blocks: TASK-046, TASK-047, TASK-048, TASK-049, TASK-050, TASK-051, TASK-052.

**Acceptance Criteria:**
- `auto h = ws.add_hook(hook_phase::response_sent, [](auto&){});` compiles and returns a `hook_handle`.
- `h.remove();` removes the registration; a second call is a no-op.
- Destruction of a non-detached `hook_handle` removes the registration.
- A test verifies `any_hooks_[phase]` flips true on the first registration of a phase and false on the last removal.
- A test verifies passing a callable with the wrong signature for a phase fails to compile (`static_assert` or template overload selection — your call).
- Public-header hygiene CI still passes; the four new headers each carry the include-guard pair.
- No phase actually fires yet — verified by registering one hook on every phase and observing zero invocations across a complete request/response cycle. (Phases start firing in TASK-046..051.)
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-001, PRD-HOOK-REQ-002, PRD-HOOK-REQ-007, PRD-HOOK-REQ-008
**Related Decisions:** DR-012, §4.10, §5.6

**Status:** Done
