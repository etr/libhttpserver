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
1. Handler throws `std::exception` → caught, logged via `error_logger`, `internal_error_handler` invoked with `e.what()`, response sent (default 500).
2. Handler throws non-`std::exception` → caught with `catch (...)`, logged generically, `internal_error_handler` invoked with `"unknown exception"`.
3. Library-internal exception in dispatch (allocation failure, body materialization error) → same path as (1)/(2).
4. `internal_error_handler` itself throws → library logs and sends a hardcoded 500 with empty body.
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
│   ├── http_method.hpp                   # NEW — http_method + method_set
│   ├── http_arg_value.hpp
│   ├── http_utils.hpp
│   ├── string_utilities.hpp
│   ├── create_webserver.hpp
│   ├── create_test_request.hpp
│   ├── file_info.hpp
│   └── detail/                          # NOT installed (existing convention)
│       ├── webserver_impl.hpp            # NEW
│       ├── http_request_impl.hpp         # NEW
│       ├── body.hpp                      # NEW — detail::body + subclasses
│       ├── http_endpoint.hpp             # existing
│       └── modded_request.hpp            # existing
└── *.cpp                                  # implementations
```

Public headers gate on `_HTTPSERVER_HPP_INSIDE_` or `HTTPSERVER_COMPILATION`. `detail/` headers gate on `HTTPSERVER_COMPILATION` only (consumers cannot reach in). `Makefile.am` continues to install `httpserver/*.hpp` and exclude `httpserver/detail/`.

---
