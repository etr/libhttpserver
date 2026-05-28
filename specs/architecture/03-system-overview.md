## 3) System Overview

### 3.1 High-level shape

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Consumer translation unit                       │
│                       #include <httpserver.hpp>                        │
│                                                                        │
│  webserver  ──→  http_request  ──→  http_resource / lambda handler    │
│      │                                                ↓                │
│      ↓                                          http_response          │
│   (PIMPL)                                       (value type, SBO body) │
└──────────┬───────────────────────────────────────────────────────────┘
           │ (no backend types crossed)
           │
┌──────────┴───────────────────────────────────────────────────────────┐
│                        libhttpserver.so internals                       │
│                                                                         │
│  webserver::impl (MHD_Daemon, route table, mutex, bans set)            │
│     ├── route table: { exact: hash, param/prefix: radix, regex: chain} │
│     ├── per-connection arena (std::pmr::monotonic_buffer_resource)     │
│     └── http_request::impl (allocated from connection's arena)         │
│                                                                         │
│  detail::body (polymorphic; subclasses string/file/iovec/pipe/         │
│                deferred/empty live in detail/body.hpp)                  │
└──────────┬───────────────────────────────────────────────────────────┘
           │
┌──────────┴───────────────────────────────────────────────────────────┐
│                       libmicrohttpd (C backend)                         │
│             MHD_Daemon, MHD_Connection, MHD_Response                    │
└────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Component summary

| Component | Responsibility | Implementation |
|---|---|---|
| `webserver` | Lifecycle, route registration, IP block list, MHD daemon ownership | PIMPL via `std::unique_ptr<webserver_impl>` |
| `http_request` | Per-request inputs (path, method, headers, args, body, TLS metadata) | PIMPL via `std::unique_ptr<http_request_impl>`; impl allocated from per-connection arena |
| `http_response` | Response value: status, headers, footers, cookies, body | Non-PIMPL value type; polymorphic body in 64-byte SBO buffer with heap fallback |
| `http_resource` | Class-form handler (state shared across HTTP methods of one resource) | Public abstract base; allow-mask held as `method_set` (`uint32_t` bitmask) |
| `websocket_handler` | Per-endpoint WebSocket protocol handler | Public abstract base; registered via `unique_ptr` / `shared_ptr` overloads |
| `detail::body` | Polymorphic body kinds (string / file / iovec / pipe / deferred / empty) | Internal hierarchy in `src/httpserver/detail/body.hpp` |
| Route table | Path → (method_set, handler) lookup | `unordered_map` (exact) + radix tree (parameterized + prefix) + regex chain (fallback) |

---
