### 4.11 Dispatch pipeline (request-processing services)

**Responsibility:** The request-processing behavior of the server — everything between a libmicrohttpd callback firing and a response being queued — factored out of the `webserver_impl` god-object (DR-014) into seven per-server **behavior services**. Each is an internal `httpserver::detail` type gated on `HTTPSERVER_COMPILATION`; none appears on the public surface or ABI. (The connection-lifecycle callbacks — `connection_notify` / `policy_callback` / `request_completed` — were *not* extracted into an eighth service; they stay in the MHD-adapter layer, see below and DR-014.)

**What a behavior service is.** Distinct from a *state collaborator* (route_table/hook_bus/… — owns a mutex + data): a behavior service owns *logic*, not state. It is constructed once per `webserver_impl`, holds only `const&` references to its dependencies, owns no mutable state, takes no locks, and operates on the per-request `detail::modded_request` passed in by reference. Because it is stateless and lock-free it is inherently shareable across MHD worker threads; the decomposition adds no synchronization.

**The seven services.**

| Service (`src/httpserver/detail/…hpp`, `src/detail/…cpp`) | Owns | Constructed with |
|---|---|---|
| `error_pages` | not_found / method_not_allowed / internal_error synthesis, `run_internal_error_handler_safely` | `const webserver_config&` |
| `response_materializer` | `http_response` → `MHD_Response`, decorate + queue, digest-challenge queueing, null-response fallback | `error_pages&`, `hook_dispatcher&`, digest opaque, `const webserver_config&` |
| `hook_dispatcher` | the four gated `fire_*_gated` helpers + the eleven per-phase forwarders over `hook_bus` | `hook_bus&`, `const webserver_config&` |
| `upload_pipeline` | `process_file_upload`, upload-stream lifecycle, post-iterator target | `const webserver_config&` |
| `websocket_upgrader` | RFC-6455 handshake validate/complete + the upgrade callback (`HAVE_WEBSOCKET`) | `ws_registry&` |
| `request_dispatcher` | `finalize_answer` orchestration, route resolution, auth-skip, handler invocation, 405 path | `route_table&`, `hook_dispatcher&`, `error_pages&`, `response_materializer&`, `const webserver_config&` |
| `request_pipeline` | `answer_to_connection` body, first/second body steps, `complete_request` | `const webserver_config&`, `hook_dispatcher&`, `request_dispatcher&` |

**Dependency graph (a DAG).**

```
request_pipeline
   ├─▶ hook_dispatcher ─▶ hook_bus
   └─▶ request_dispatcher
          ├─▶ route_table
          ├─▶ hook_dispatcher
          ├─▶ error_pages ─▶ (const webserver_config&)
          └─▶ response_materializer
                 ├─▶ error_pages
                 └─▶ hook_dispatcher
websocket_upgrader   ─▶ ws_registry
```

Acyclic, so the composition root wires it with plain member references — no mediator. Services store references at construction but never invoke a dependency during their own constructor, so binding a reference to a sibling member is well-defined irrespective of member-declaration order (`-Wreorder -Werror` guards accidental reorders).

**The MHD adapter layer.** libmicrohttpd calls in through fixed-signature C trampolines carrying a `void* cls` closure: `answer_to_connection`, `request_completed`, `connection_notify`, `policy_callback`, `post_iterator`, `uri_log`, `error_log`, `unescaper_func`, `upgrade_handler`, plus the GnuTLS `psk_cred_handler_func` / `sni_cert_callback_func`. These stay `static`/free functions — they unpack `cls` (a `webserver_impl*`, `webserver*`, `modded_request*`, or `ws_upgrade_data*`) and forward into a service. They are the C-ABI boundary, not behavior. `connection_notify`, `policy_callback`, and `request_completed` are the fullest members of this layer: they keep their per-connection arena (`connection_state` new/delete via `socket_context`), accept-policy, and request-teardown glue inline, delegating only the extracted free functions (`classify_decision`, `make_peer_address`) and the `ip_access_control` / `hook_dispatcher` collaborators — which is why they were not carved into a separate service (DR-014).

**Free functions (not services).** Pure, instance-stateless helpers live as free functions in `httpserver::detail`, not one-method classes: `log_dispatch_error(const webserver_config&, std::string_view)` (called by every error path — a free function keeps it dependency-edge-free), `serialize_allow_methods` / `format_allow_header`, `resolve_method_callback`, `materialize_response`, `decorate_mhd_response`, `handle_post_form_arg`, `manage_upload_stream`.

**Per-request context.** `detail::modded_request` (per connection, arena/PMR-allocated per DR-003b) is the object the pipeline threads through the MHD callback sequence: `uri_log` allocates it; `answer_to_connection` (first invocation) stamps `start_time` / `standardized_url` / `method_enum` and builds the `http_request`; the body steps accumulate upload data; `finalize_answer` stages `response`; `request_completed` fires the terminal hook and deletes it. The services read and write its fields but do not own it.

**Key design notes:**
- Config is read-only to every service (`const webserver_config&`); no service holds the `webserver*` back-pointer. The back-pointer stays on the composition root for the members that genuinely need the owning `webserver*`.
- The decomposition is behavior-only: no new mutexes, no new shared mutable state, no change to lock order (route_table → resource hook mutex → hook_table mutex, per §4.10).
- Structural acceptance is enforced by the same gates as the rest of `src/`: 500 SLOC per file, CCN ≤ 10 per function, CPD ≥ 100-token duplication.

**Related decisions:** DR-014 (this decomposition), DR-012 (hook bus — the state-collaborator precedent and the phase/firing map in §4.10), DR-007 (route table), DR-003b (request PIMPL / arena allocation), DR-009 §5.2 (exception → 500 contract that `error_pages` implements).

---
