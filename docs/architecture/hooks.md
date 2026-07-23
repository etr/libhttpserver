# Hooks cookbook

> A user-facing reference for the 11 lifecycle hook phases: what each observes, which can short-circuit, and how to register them.
> Component spec: [`specs/architecture/04-components/hooks.md`](../../specs/architecture/04-components/hooks.md) · mechanics in [request-flow](request-flow.md).

## Where the phases fire

```mermaid
flowchart LR
    O[connection_opened] --> A[accept_decision] --> RR[request_received] --> BC[body_chunk] --> RES[route_resolved] --> BH[before_handler] --> HX[handler_exception] --> AH[after_handler] --> RS[response_sent] --> RC[request_completed] --> CC[connection_closed]
```

Five phases can **short-circuit** (return `hook_action::respond_with(...)` to end the request early): `request_received`, `body_chunk`, `before_handler`, `handler_exception`, `after_handler`. The rest are **observe-only** (return `void`; the chain always runs to completion).

## Phase catalog

| # | Phase | Context struct | Key fields | Kind | Scope |
|---|---|---|---|---|---|
| 0 | `connection_opened` | `connection_open_ctx` | `peer_address peer` | observe | server |
| 1 | `accept_decision` | `accept_ctx` | `peer_address peer` · `bool accepted` · `optional<string_view> reason` | observe | server |
| 2 | `request_received` | `request_received_ctx` | `http_request* request` · `steady_clock::time_point received_at` | **short-circuit** | server |
| 3 | `body_chunk` | `body_chunk_ctx` | `http_request* request` · `span<const byte> chunk` · `uint64_t offset` · `bool is_final` | **short-circuit** | server |
| 4 | `route_resolved` | `route_resolved_ctx` | `const http_request* request` · `optional<route_descriptor> matched` · `const http_resource* resource` | observe | server |
| 5 | `before_handler` | `before_handler_ctx` | `http_request* request` · `optional<route_descriptor> matched` · `http_method method` · `const http_resource* resource` | **short-circuit** | server **+ route** |
| 6 | `handler_exception` | `handler_exception_ctx` | `const http_request* request` · `exception_ptr exception` · `string_view message` | **short-circuit** | server **+ route** |
| 7 | `after_handler` | `after_handler_ctx` | `const http_request* request` · `http_response* response` (mutable) | **short-circuit / replace** | server **+ route** |
| 8 | `response_sent` | `response_sent_ctx` | `const http_request* request` · `const http_response* response` · `int status` · `size_t bytes_queued` · `nanoseconds elapsed` | observe | server **+ route** |
| 9 | `request_completed` | `request_completed_ctx` | `const http_request* request` · `const http_response* resp` · `bool succeeded` · `steady_clock::duration duration` | observe | server **+ route** |
| 10 | `connection_closed` | `connection_close_ctx` | `peer_address peer` | observe | server |

**All pointers are valid only for the synchronous duration of the hook call — never capture them past return.**

Shared structs:
- `peer_address` — `family fam` (`unspec`/`ipv4`/`ipv6`) · `array<uint8_t,16> bytes` (network order) · `uint16_t port` (host order) · `string to_string()`.
- `route_descriptor` — `string_view path_template` (per-request storage — do not capture) · `method_set methods` · `bool is_prefix`.

### `hook_action` (the short-circuit payload)

Move-only wrapper over `optional<http_response>`:

| Method | Meaning |
|---|---|
| `hook_action::pass()` | continue the chain |
| `hook_action::respond_with(http_response r)` | short-circuit; `r` is sent, later hooks in the phase skip |
| `is_pass()` | true if no payload |
| `take_response() &&` | consume the payload (precondition `!is_pass()`) |

**`after_handler` can replace the response two ways:** mutate in place via the mutable `response*` (rewrite headers/status, keep body), **or** `respond_with(r2)` to fully replace it — the resource's reply is discarded and never hits the wire.

## Registering hooks

**Server-wide** — `webserver::add_hook(phase, fn)`, all 11 phases. The `phase` argument is validated against the overload's signature at runtime (`std::invalid_argument` on mismatch or empty `fn`). Returns a `hook_handle`.

```cpp
// observe: void(const Ctx&) ; short-circuit: hook_action(Ctx&)
auto h = ws.add_hook(hook_phase::response_sent,
    std::function<void(const response_sent_ctx&)>(
        [](const response_sent_ctx& c){ /* log c.status, c.bytes_queued, c.elapsed */ }));
```

**Per-route** — `http_resource::add_hook(phase, fn)`, only the **5 post-route-resolution** phases: `before_handler`, `handler_exception`, `after_handler`, `response_sent`, `request_completed`. Any other phase throws `std::invalid_argument`. Per-route hooks fire **after** the server-wide chain at the same phase, and only if that chain did not short-circuit. The per-route table is lazily allocated on first `add_hook`; copying an `http_resource` shares the same table.

**Execution order at a phase:** server-wide (registration order) → per-route (registration order); a short-circuit skips everything after it.

### `hook_handle` — the RAII receipt

Move-only; dropping an **armed** handle removes the registration.

| Operation | Effect |
|---|---|
| destructor | calls `remove()` unless detached / moved-from / already removed |
| `remove()` | idempotent; erases the registration, then disarms |
| `std::move(h).detach()` | keeps the registration for the server's lifetime; returns an inert receipt |

⚠️ A server-wide handle holds a **non-owning `webserver_impl*`** — an armed handle must not outlive its `webserver` (use-after-free, CWE-416). Per-route handles hold a `weak_ptr`; if the resource dies first, `remove()` becomes a safe no-op.

## v1 compatibility aliases

The old install-time callbacks map onto phases (all wired at construction, immutable afterward — use `add_hook` for runtime registration):

| v1 setter | Phase | Notes |
|---|---|---|
| `log_access(fn)` | `response_sent` | dedicated `log_access_alias_` slot; fires after user `response_sent` hooks |
| `internal_error_handler(fn)` | `handler_exception` | last-position fallback (`handler_exception_alias_`) before the hardcoded 500 |
| `auth_handler(fn)` | `before_handler` | registered **first**; **fail-closed** — an exception in the auth callable short-circuits with **500** directly, not via `handler_exception` |
| `method_not_allowed_handler(fn)` | `before_handler` | registered second; 405 + `Allow` when method not permitted |
| `not_found_handler(fn)` | `route_resolved` | observation stub; the 404 body is synthesized separately |

## Recipes

Representative examples from [`examples/`](../../examples/) and [`test/integ/`](../../test/integ/):

- **Per-route auth** — `examples/per_route_auth.cpp`: a `before_handler` hook on `/private` reads `ctx.request->get_user()/get_pass()`, constant-time compares, and returns `pass()` or `respond_with(http_response::unauthorized("Basic","private-realm","Unauthorized"))`. Sibling `/public` is untouched.
- **Early 413 on upload** — `examples/early_413.cpp`: a `request_received` hook checks `Content-Length` against a cap and returns `respond_with(413)` so the body is never read and the handler never runs.
- **Common Log Format access log** — `examples/clf_access_log.cpp`: a `response_sent` hook emits a CLF line from `ctx.status` / `ctx.bytes_queued` / `ctx.elapsed` (control-char sanitized). The modern replacement for v1 `log_access`.
- **Denied-IP logging** — `examples/denied_ip_log.cpp`: an `accept_decision` hook logs each rejection (`ctx.accepted==false`, `ctx.reason`). Observation-only — throwing does not flip the verdict.
- **Replace the response after the handler** — `test/integ/hooks_after_handler_replaces_response.cpp`: an `after_handler` hook returns `respond_with(r2)`; the resource's reply is discarded.
- More coverage: `hooks_body_chunk_*`, `hooks_handler_exception_chain.cpp`, `hooks_response_sent_carries_status_bytes_timing.cpp`, `hooks_connection_lifecycle.cpp`, `hooks_per_route_*`.

---
*See also: [request-flow](request-flow.md) (exactly where each phase fires) · [errors](errors.md) (the `handler_exception` phase and the 500 fallback).*
