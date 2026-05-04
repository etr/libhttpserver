## 1) Executive Summary

libhttpserver is a C++ HTTP server library wrapping libmicrohttpd. v2.0 is a clean breaking release whose architectural goal is to **hide the C backend from the public ABI** and **fit 2026 C++ idioms** without requiring users to subclass, manage raw pointers, or mirror the library's build flags.

The design rests on five load-bearing choices: a **C++20 floor**; **PIMPL on `webserver` and `http_request`** with a backend-free public surface; a **non-PIMPL value-typed `http_response`** with a polymorphic body held in a 64-byte SBO buffer that falls back to heap; **handler-returns-by-value** as the canonical signature; and a **route table with three structures** (hash for exact paths, radix for parameterized + prefix, regex chain for fallback). The remaining decisions — thread-safety contract, error propagation, deferred/websocket lifecycle, ABI versioning — are documentation and consistency rather than novel mechanism.

The architecture preserves libmicrohttpd as the only backend (no pluggable backends in scope) but makes its presence invisible in `<httpserver.hpp>`. It commits to value semantics where they fit and PIMPL where they don't, refusing to apply either uniformly.

---
