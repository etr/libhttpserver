# Request lifecycle & routing flow

> How an HTTP request travels from the libmicrohttpd callback to the wire, for libhttpserver v2.0.
> Quick-view below; the full step-by-step page (28 ordered steps, the body loop, the WebSocket branch, the hook rail) is **[`request-flow.html`](request-flow.html)** (open in a browser).

One HTTP request is not one function call. libmicrohttpd drives the exchange through a fixed sequence of C-ABI callbacks (static `webserver_impl` trampolines in `webserver_callbacks.cpp`), each forwarding into a behavior service. `answer_to_connection` is called **1..N times** — once for a bodyless `GET`, many times while a `POST` body streams in — but resolves to exactly one `finalize_answer`.

## The callback spine

```mermaid
sequenceDiagram
    autonumber
    participant MHD as libmicrohttpd
    participant AD as webserver_impl trampolines
    participant PL as request_pipeline
    participant DP as request_dispatcher
    participant RT as route_table
    participant RM as response_materializer

    MHD->>AD: policy_callback · IP ACL
    Note over AD: ip_access_control.classify → accept? · ◈ accept_decision
    MHD->>AD: connection_notify STARTED
    Note over AD: new connection_state (arena) · ◈ connection_opened
    MHD->>AD: uri_log
    Note over AD: new connection_context → *con_cls

    MHD->>AD: answer_to_connection · first · request==nullptr
    Note over AD: canonicalize URL · resolve_method_callback
    AD->>PL: requests_answer_first_step
    Note over PL: build http_request · ◈ request_received short-circuit
    alt has body · POST or PUT
        loop until zero-size chunk
            MHD->>AD: answer_to_connection · body chunk
            AD->>PL: requests_answer_second_step
            Note over PL: ◈ body_chunk · grow_content / post_iterator → upload_pipeline
        end
    end

    MHD->>AD: answer_to_connection · terminal · size==0
    AD->>PL: requests_answer_second_step → complete_request
    PL->>DP: finalize_answer
    Note over DP: try_ws_upgrade → 101 branch bypasses the rest
    DP->>RT: lookup_v2 · exact→cache→radix→regex
    RT-->>DP: entry + captured_params · else 404
    Note over DP: ◈ route_resolved · ◈ before_handler auth · is_allowed → 405
    Note over DP: pointer-to-member → render_get · catch → ◈ handler_exception → 500
    Note over DP: ◈ after_handler may replace response
    DP->>RM: materialize_and_queue_response
    Note over RM: response_body.materialize → decorate → MHD_queue_response ◀ SEND · ◈ response_sent
    MHD->>AD: request_completed
    Note over AD: ◈ request_completed · delete connection_context · reset_arena
    MHD->>AD: connection_notify CLOSED
    Note over AD: ◈ connection_closed · delete connection_state
```

## Four-tier route resolution

`route_table::lookup_v2` — cheapest tier first, first hit wins:

```mermaid
flowchart LR
    C["canonicalize path"] --> T1{"Tier 1<br/>exact_routes_ map"}
    T1 -- hit --> H["entry + captured_params"]
    T1 -- miss --> T2{"Tier 2<br/>LRU cache"}
    T2 -- hit --> H
    T2 -- miss --> T3{"Tier 3<br/>segment_trie radix"}
    T3 -- hit --> INS["cache install"] --> H
    T3 -- miss --> T4{"Tier 4<br/>regex scan"}
    T4 -- hit --> INS
    T4 -- miss --> NF["404 not found"]
```

The entry is returned **regardless of method** so the 405 path still sees it. Method → handler is chosen separately: the verb became a pointer-to-member (`render_get`/`render_post`/…) in `resolve_method_callback`, and `dispatch_resource_handler` checks `http_resource::is_allowed(method_enum)` → mismatch yields **405** + the resource's `Allow:` header.

## Where the hook phases fire

Server-wide hooks live on `hook_bus`; the five post-route-resolution phases are also **per-resource** (`resource_hook_table`, via `http_resource::add_hook`). Every phase is guarded by a relaxed-atomic `has_hooks_for` check — zero cost when unused. The full context-struct catalog and usage recipes are in **[hooks.md](hooks.md)**.

| # | Phase | Fires in | Scope | Kind |
|---|---|---|---|---|
| 1 | `connection_opened` | `connection_notify` STARTED | server | observe |
| 2 | `accept_decision` | `policy_callback` | server | observe |
| 3 | `request_received` | `requests_answer_first_step` | server | short-circuit |
| 4 | `body_chunk` | `requests_answer_second_step` (per chunk) | server | short-circuit |
| 5 | `route_resolved` | `finalize_answer` (after lookup) | server | observe |
| 6 | `before_handler` | `finalize_answer` (pre-dispatch · **auth**) | server + route | short-circuit |
| 7 | `handler_exception` | `dispatch_resource_handler` catch | server + route | short-circuit |
| 8 | `after_handler` | `finalize_answer` (post-handler) | server + route | replace resp |
| 9 | `response_sent` | `materialize_and_queue` (after queue) | server + route · log_access alias | observe |
| 10 | `request_completed` | `request_completed` callback | server + route | observe |
| 11 | `connection_closed` | `connection_notify` CLOSED | server | observe |

---
*See also: [class map](class-map.md) (the classes on each lane) · [hooks.md](hooks.md) (the hook cookbook) · [errors.md](errors.md) (the 404/405/500 origins).*
