### 4.10 Hook bus

**Responsibility:** Multi-subscriber extension surface spanning the connection → request → routing → handler → response → cleanup lifecycle. Replaces v1's patchwork of single-slot callbacks (which now exist as documented aliases per DR-012).

**Phases.** Eleven, in firing order:

| Phase | Fires at (file:symbol) | Short-circuit | Per-route eligible |
|---|---|---|---|
| `connection_opened` | `webserver.cpp:connection_notify` — `MHD_CONNECTION_NOTIFY_STARTED` | no | no |
| `accept_decision` | `webserver.cpp:policy_callback` — after YES/NO decision | no | no |
| `request_received` | `webserver_body_pipeline.cpp:requests_answer_first_step` — post header-parse, pre body-read | **yes** | no (route not yet known) |
| `body_chunk` | `webserver_body_pipeline.cpp:requests_answer_second_step` — per chunk | **yes** | no |
| `route_resolved` | `webserver.cpp:resolve_resource_for_request` — after lookup | no | n/a (boundary phase) |
| `before_handler` | `webserver.cpp:dispatch_resource_handler` — start | **yes** | yes |
| `handler_exception` | `webserver.cpp:dispatch_resource_handler` — each catch arm | **yes** (maps exception to response) | yes |
| `after_handler` | between handler return and `materialize_and_queue_response` | **yes** (replaces response) | yes |
| `response_sent` | `webserver.cpp:materialize_and_queue_response` — post `MHD_queue_response` | no | yes |
| `request_completed` | `webserver.cpp:request_completed` — NOTIFY_COMPLETED | no | yes |
| `connection_closed` | `webserver.cpp:connection_notify` — NOTIFY_CLOSED | no | no |

**Implementation.** Each phase has its own `std::vector<std::function<...>>` in `webserver_impl`, guarded by a single `std::shared_mutex hook_table_mutex_`. A per-phase `std::atomic<bool> any_hooks_[hook_phase::count_]` flag short-circuits the dispatch site to a relaxed atomic load and a compare-with-zero when no subscribers exist — the only hook-related cost on the hot request path for a server with zero hooks registered.

Per-route hooks live on `http_resource` in the same shape (per-phase vectors + `any_hooks_` flag + the resource's own `std::shared_mutex`). The dispatch path takes a `shared_lock` on the resource's hook mutex after the existing route-table `shared_lock`. **Lock order:** `route_table_mutex_` → resource hook mutex → `hook_table_mutex_` (server-wide). The lookup pipeline never holds two locks across an iteration step: it copies the vector under a shared_lock, releases, and iterates the copy.

**API surface (umbrella `<httpserver.hpp>`):**

```cpp
namespace httpserver {

enum class hook_phase {
    connection_opened, accept_decision,
    request_received, body_chunk,
    route_resolved, before_handler, handler_exception,
    after_handler, response_sent,
    request_completed, connection_closed,
    count_   // end sentinel
};

class hook_action {
 public:
    static hook_action pass();
    static hook_action respond_with(http_response r);
    bool is_pass() const noexcept;
    http_response&& take_response() &&;   // valid iff !is_pass()
};

class hook_handle {
 public:
    hook_handle() = default;
    hook_handle(hook_handle&&) noexcept;
    hook_handle& operator=(hook_handle&&) noexcept;
    ~hook_handle();                       // removes unless detached
    void remove() noexcept;
    hook_handle detach() &&;              // disarm the destructor
};

// Per-phase context structs — peer_address, request_received_ctx,
// body_chunk_ctx, route_resolved_ctx, before_handler_ctx,
// handler_exception_ctx, after_handler_ctx, response_sent_ctx,
// request_completed_ctx, connection_open_ctx, connection_close_ctx.
// All libhttpserver-defined; never expose MHD types (PRD-HDR-REQ-001).
// Observation-only contexts pass `const http_request&` / `const http_response&`;
// mutating contexts (`after_handler_ctx`) expose `http_response&`.

// On webserver — one add_hook overload per phase. Phase tag in the
// hook_phase enum selects the overload; the callable's signature is
// type-checked against the matching context struct at the call site.
hook_handle webserver::add_hook(hook_phase, std::function<...>);

// On http_resource — same shape, scoped to dispatches of this resource.
// Only the route-bound phases are accepted; the others throw
// std::invalid_argument.
hook_handle http_resource::add_hook(hook_phase, std::function<...>);

}  // namespace httpserver
```

**Concurrency.** `add_hook` and `hook_handle::remove` are safe to call from inside a hook (writer lock taken briefly). In-flight hook chains see a stable snapshot — the dispatch site copies the vector under a `shared_lock`, releases the lock, then iterates the copy. New registrations are picked up by the next firing of that phase.

**Execution order within a phase.** Server-wide hooks first (registration order), then per-route hooks (registration order). A short-circuit at any position skips the rest within the phase.

**Exception policy.** A throwing hook lands in the DR-009 §5.2 path — no new error contract. Practically: a hook throwing `std::exception` is caught, logged via `log_error`, and routed to the `handler_exception` chain (which itself is hookable — a `handler_exception` hook throwing recurses one level to the `internal_error_handler` alias and then to the hardcoded empty-body 500).

**Zero-cost-when-unused.** Per-phase `std::atomic<bool> any_hooks_` flag. Verified by `bench_hook_overhead` (acceptance criterion of TASK-052) — a server with zero hooks should be within microbench noise of the pre-hook-system baseline.

**v1 aliases.** The following v1-derived setters remain on `create_webserver` for ergonomic call sites; the Doxygen comment on each setter, the corresponding README section, and `RELEASE_NOTES.md` identify them as sugar that internally registers a hook at the indicated phase:

| Setter | Equivalent hook |
|---|---|
| `log_access(fn)` | `response_sent` |
| `not_found_handler(fn)` | `route_resolved` (when route is `nullopt`; runs after any user `route_resolved` hooks that did not short-circuit) |
| `method_not_allowed_handler(fn)` | `before_handler` (when `is_allowed(method)` is false; runs after any user `before_handler` hook that did not short-circuit) |
| `internal_error_handler(fn)` | `handler_exception` (last-position fallback in the chain) |
| `auth_handler(fn)` | `before_handler` (runs before the method-allowed check) |
| `log_error(fn)` | *not* a hook alias — it is an MHD-level callback for backend errors, distinct from the request lifecycle |
| `file_cleanup_callback(fn)` | *not* a hook alias — file-upload cleanup is a separate post-upload concern, not a lifecycle phase |

**Related requirements:** PRD-HOOK-REQ-001..009.
**Related decisions:** DR-012, §5.6.

---
