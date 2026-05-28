## 5) Cross-cutting concerns

### 5.1 Threading model

**Contract (committed in DR-8):**
1. `webserver` public methods are thread-safe and re-entrant from inside a handler. Exceptions: `stop()` and `~webserver()` deadlock if called from within a handler thread (they wait for that very thread to drain). Documented.
2. Handlers run concurrently on MHD worker threads. The same lambda or `http_resource` instance is invoked from many threads simultaneously. User-side state must be synchronized by the user.
3. `http_request` is single-threaded per request. Sharing it across threads is undefined.
4. `http_response` is value-typed with exclusive ownership. Returning it transfers it.

**Internal locks:**
- `route_table_mutex` (`std::shared_mutex`) — registration vs lookup.
- `route_cache_mutex` (`std::mutex`) — LRU cache promotion.
- `bans_mutex` (`std::shared_mutex`) — block list.
- `mutexwait` / `mutexcond` (`pthread_mutex_t` / `pthread_cond_t`) — start/stop handshake (kept as POSIX primitives because MHD's start path expects them).

### 5.2 Error propagation

**Contract (committed in DR-9):**
1. Handler throws `std::exception` → caught, logged via the `log_error` callback, `internal_error_handler` invoked with `e.what()`, response sent (default 500).
2. Handler throws non-`std::exception` → caught with `catch (...)`, logged generically via `log_error`, `internal_error_handler` invoked with `"unknown exception"`.
3. Library-internal exception in dispatch (allocation failure, body materialization error) → same path as (1)/(2).
4. `internal_error_handler` itself throws → library logs via `log_error` and sends a hardcoded 500 with empty body.
5. `feature_unavailable` is a normal `std::runtime_error`; no special status mapping. Users who care translate it explicitly.
6. There is no throw-as-status idiom. Users wanting 404/400/etc. construct the response by value: `return http_response::empty().with_status(404);`.

### 5.3 Memory and allocation hot paths

| Object | Allocations per instance | Notes |
|---|---|---|
| `webserver` | 1 (impl) + N (route table grow) | One per process |
| `http_request` | 1 (impl) — arena-allocated from per-connection pool | Reset between requests on keep-alive connections |
| `http_response` (empty / small string body) | 0 (SBO covers body) | Headers/footers/cookies maps still allocate per insertion |
| `http_response` (large content, file, iovec, deferred) | 1 (body content); 0 for the body object (SBO) | Same content allocations as v1 |

### 5.4 ABI versioning

SOVERSION bump only. No inline namespace, no symbol-versioning script. v1.x is end-of-life on the day v2.0 ships (PRD §1, OQ-007). Distros package `libhttpserver2` parallel-installable with `libhttpserver1` via standard SOVERSION mechanics.

### 5.5 Header layout

```
src/
├── httpserver.hpp                       # umbrella, defines _HTTPSERVER_HPP_INSIDE_
├── httpserver/                           # PUBLIC, installed
│   ├── webserver.hpp
│   ├── http_request.hpp
│   ├── http_response.hpp
│   ├── http_resource.hpp
│   ├── websocket_handler.hpp
│   ├── constants.hpp                     # NEW — httpserver::constants namespace (TASK-006)
│   ├── http_method.hpp                   # NEW — http_method + method_set
│   ├── feature_unavailable.hpp           # NEW — httpserver::feature_unavailable exception (TASK-003)
│   ├── iovec_entry.hpp                   # NEW — httpserver::iovec_entry POD (TASK-004), pulled in by http_response.hpp
│   ├── body_kind.hpp                     # NEW — body_kind discriminator enum (TASK-010)
│   ├── http_arg_value.hpp
│   ├── http_utils.hpp
│   ├── string_utilities.hpp
│   ├── create_webserver.hpp
│   ├── create_test_request.hpp
│   ├── file_info.hpp
│   ├── hook_phase.hpp                   # NEW — hook_phase enum (DR-12)
│   ├── hook_action.hpp                  # NEW — hook_action token (DR-12)
│   ├── hook_handle.hpp                  # NEW — hook_handle RAII (DR-12)
│   ├── hook_context.hpp                 # NEW — per-phase context structs (DR-12)
│   └── detail/                          # NOT installed (existing convention)
│       ├── webserver_impl.hpp            # NEW
│       ├── http_request_impl.hpp         # NEW
│       ├── body.hpp                      # NEW — detail::body + subclasses
│       ├── http_endpoint.hpp             # existing
│       └── modded_request.hpp            # existing
└── *.cpp                                  # implementations
```

Public headers gate on `_HTTPSERVER_HPP_INSIDE_` or `HTTPSERVER_COMPILATION`. `detail/` headers gate on `HTTPSERVER_COMPILATION` only (consumers cannot reach in). `Makefile.am` continues to install `httpserver/*.hpp` and exclude `httpserver/detail/`.

### 5.6 Hook lifecycle

**Contract (committed in DR-12):**

1. **Eleven phases** (`connection_opened`, `accept_decision`, `request_received`, `body_chunk`, `route_resolved`, `before_handler`, `handler_exception`, `after_handler`, `response_sent`, `request_completed`, `connection_closed`). See §4.10 for the firing-site map.
2. **Multi-subscriber per phase.** Execution order: server-wide hooks first (registration order), then per-route hooks on `http_resource` (registration order).
3. **Short-circuit** is allowed at the four pre-handler phases (`request_received`, `body_chunk`, `before_handler`, `handler_exception`) and at the `after_handler` post-handler phase. A hook short-circuits by returning `hook_action::respond_with(response)`. At pre-handler phases this skips the resource handler; at `after_handler` it replaces the in-flight response. Remaining hooks at the phase are skipped in both cases. `response_sent`, `request_completed`, `connection_opened`, `connection_closed`, `accept_decision`, `route_resolved` are observation-only.
4. **Exceptions** thrown by a hook are caught and routed through §5.2 / DR-9 — same path as a throwing resource handler. The one exception: the `handler_exception` chain itself continues to the next hook when one of its hooks throws, because the whole point of the chain is exception recovery.
5. **Thread safety.** `webserver::add_hook`, `http_resource::add_hook`, and `hook_handle::remove` are safe to call from inside a hook (mirrors §5.1 for `register_resource`). The dispatch site copies the relevant phase vector under a `shared_lock`, releases the lock, then iterates the copy — so an in-flight chain sees a stable snapshot.
6. **Zero-cost when unused.** Per-phase `std::atomic<bool> any_hooks_` short-circuits the hot path to a relaxed atomic load + compare-with-zero when no subscribers exist. Verified by `bench_hook_overhead`.

**Lock order (additive to §5.1):** `route_table_mutex_` → resource's `hook_table_mutex_` → server-wide `hook_table_mutex_`. No two are held across an iteration step; each is taken, the vector is copied, and the lock is released before invocation.

**v1-shorthand aliases.** `log_access`, `not_found_handler`, `method_not_allowed_handler`, `internal_error_handler`, `auth_handler` survive on `create_webserver` as documented sugar. Each setter's Doxygen, the README, and `RELEASE_NOTES.md` identify it as an alias that internally registers a hook at the corresponding phase (see §4.10 table). `log_error` and `file_cleanup_callback` are NOT hook aliases — they are MHD-level / post-upload concerns distinct from the request lifecycle.

---
