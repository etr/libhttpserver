# libhttpserver v2.0 — architecture diagrams

Architecture references for the v2.0 (`feature/v2.0`) codebase.

**Two hero diagrams** — spatial maps, best viewed as the rich self-contained HTML (no external assets, light/dark aware); Mermaid quick-views render inline below.

| Diagram | GitHub-native | Rich page |
|---|---|---|
| **Class, relationship & filesystem map** | [Mermaid ↓](#1-class-relationship--filesystem-map) | [`class-map.html`](class-map.html) |
| **Request lifecycle & routing flow** | [Mermaid ↓](#2-request-lifecycle--routing-flow) | [`request-flow.html`](request-flow.html) |

**Four deep dives** — reference-heavy topics, as Markdown (Mermaid + tables, render inline on GitHub).

| Doc | For | Covers |
|---|---|---|
| [**threading.md**](threading.md) | contributors | DR-008 concurrency contract, full mutex inventory, lock ordering, Helgrind lane |
| [**errors.md**](errors.md) | contributors | DR-009 propagation, every 4xx/5xx origin, the handler-exception path, config knobs |
| [**hooks.md**](hooks.md) | API users | the 11 phases, context fields, short-circuit vs observe, `hook_handle`, recipes |
| [**features.md**](features.md) | packagers / API users | `HAVE_*` detection, gated symbols, `features()`, `feature_unavailable`, build matrix |

> **Colour language** (shared by both diagrams): composition-root = blue · state collaborator = amber · behavior service = teal · MHD C-ABI adapter = purple · domain / value type = slate.

---

## 1. Class, relationship & filesystem map

Post-**DR-014**, `webserver` is a thin façade over `webserver_impl`, which is a **pure composition root** holding **5 state collaborators** (own their mutexes + data) and **7 behavior services** (stateless; hold `const&` into state and each other), plus a **static MHD adapter facet** (the C-ABI trampolines). Ownership is strictly linear and top-down; services form an acyclic DAG with no back-pointer (the sole exception: `daemon_lifecycle` needs `webserver_impl*` to read broad config while building the MHD option array).

Stereotypes below encode the role: `<<state>>` = state collaborator (owns mutex + data), `<<behavior>>` = behavior service (stateless), `<<adapter>>` = MHD C-ABI facet. Solid diamond (`*--`) = owns by value; dashed arrow (`..>`) = holds `const&` reference.

```mermaid
classDiagram
    direction LR
    class create_webserver
    class webserver
    class webserver_impl {
        <<adapter>>
    }
    create_webserver --> webserver : builds
    webserver *-- webserver_impl : owns

    class daemon_lifecycle {
        <<state>>
    }
    class route_table {
        <<state>>
    }
    class hook_bus {
        <<state>>
    }
    class ip_access_control {
        <<state>>
    }
    class ws_registry {
        <<state>>
    }
    webserver_impl *-- daemon_lifecycle
    webserver_impl *-- route_table
    webserver_impl *-- hook_bus
    webserver_impl *-- ip_access_control
    webserver_impl *-- ws_registry

    class error_pages {
        <<behavior>>
    }
    class hook_dispatcher {
        <<behavior>>
    }
    class response_materializer {
        <<behavior>>
    }
    class upload_pipeline {
        <<behavior>>
    }
    class websocket_upgrader {
        <<behavior>>
    }
    class request_dispatcher {
        <<behavior>>
    }
    class request_pipeline {
        <<behavior>>
    }
    webserver_impl *-- error_pages
    webserver_impl *-- hook_dispatcher
    webserver_impl *-- response_materializer
    webserver_impl *-- upload_pipeline
    webserver_impl *-- websocket_upgrader
    webserver_impl *-- request_dispatcher
    webserver_impl *-- request_pipeline

    hook_dispatcher ..> hook_bus
    response_materializer ..> error_pages
    response_materializer ..> hook_dispatcher
    websocket_upgrader ..> ws_registry
    request_dispatcher ..> route_table
    request_dispatcher ..> hook_dispatcher
    request_dispatcher ..> error_pages
    request_dispatcher ..> response_materializer
    request_dispatcher ..> websocket_upgrader
    request_pipeline ..> hook_dispatcher
    request_pipeline ..> request_dispatcher
```

`daemon_lifecycle` (HAVE_WEBSOCKET builds also wire `ws_registry` + `websocket_upgrader`) — `route_table` owns `route_entry` / `segment_trie` / `route_cache`; `hook_bus` holds the 11 server-wide phase vectors; `response_materializer` turns `http_response` into an `MHD_Response`; `request_pipeline` is the re-entrant body-accumulation state machine. Full descriptions and per-class file locations: [`class-map.html`](class-map.html).

**Per-request / per-connection state** (threaded through the services):

```mermaid
classDiagram
    direction LR
    class connection_context
    class connection_state
    class http_request
    class http_response
    class http_resource
    class response_body {
        <<abstract>>
    }

    connection_context *-- http_request : owns
    connection_context *-- http_response : owns optional
    connection_context ..> http_resource : weak_ptr
    http_response *-- response_body : owns 64B SBO
    response_body <|-- empty_response_body
    response_body <|-- string_response_body
    response_body <|-- file_response_body
    response_body <|-- iovec_response_body
    response_body <|-- pipe_response_body
    response_body <|-- deferred_response_body
    response_body <|-- digest_challenge_response_body
```

**Filesystem convention.** Public surface in `src/httpserver/` (installed); internal detail headers in `src/httpserver/detail/` (never installed); implementations in `src/` and `src/detail/`. `webserver` = one façade TU (`src/webserver.cpp`); `webserver_impl` = two TUs (`src/detail/webserver_impl.cpp` composition root + `src/detail/webserver_callbacks.cpp` MHD adapter). The full per-class header/cpp locations are in [`class-map.html`](class-map.html).

---

## 2. Request lifecycle & routing flow

One HTTP request is not one function call. libmicrohttpd drives the exchange through a fixed sequence of C-ABI callbacks (static `webserver_impl` trampolines in `webserver_callbacks.cpp`), each forwarding into a behavior service. `answer_to_connection` is called **1..N times** — once for a bodyless `GET`, many times while a `POST` body streams in — but resolves to exactly one `finalize_answer`.

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

**Four-tier route resolution** (`route_table::lookup_v2`, cheapest first, first hit wins):

```mermaid
flowchart LR
    C[canonicalize path] --> T1{Tier 1<br/>exact_routes_ map}
    T1 -- hit --> H[entry + captured_params]
    T1 -- miss --> T2{Tier 2<br/>LRU cache}
    T2 -- hit --> H
    T2 -- miss --> T3{Tier 3<br/>segment_trie radix}
    T3 -- hit --> INS[cache install] --> H
    T3 -- miss --> T4{Tier 4<br/>regex scan}
    T4 -- hit --> INS
    T4 -- miss --> NF[404 not found]
```

The entry is returned **regardless of method** so the 405 path still sees it. Method → handler is chosen separately: the verb became a pointer-to-member (`render_get`/`render_post`/…) in `resolve_method_callback`, and `dispatch_resource_handler` checks `http_resource::is_allowed(method_enum)` → mismatch yields **405** + the resource's `Allow:` header.

### The 11 hook phases (firing order)

Server-wide hooks live on `hook_bus`; the five post-route-resolution phases are also **per-resource** (`resource_hook_table`, via `http_resource::add_hook`). Every phase is guarded by a relaxed-atomic `has_hooks_for` check — zero cost when unused.

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

## Keeping these current

These diagrams describe `feature/v2.0` as of the DR-014 decomposition. When the composition root's collaborators, the callback spine, the route tiers, or the hook phases change, update **both** the Mermaid blocks above and the corresponding `.html` page (they are hand-authored, not generated). The rich pages are also published as live Claude artifacts — see the team's shared links.
