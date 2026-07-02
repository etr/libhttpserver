## 9) Testing strategy

The architecture itself does not prescribe test frameworks (out of architecture scope), but it does name the test surfaces that need first-class coverage given v2.0's structural changes:

1. **Header hygiene** (PRD-HDR-REQ-001..003): a CI test compiles a TU containing only `#include <httpserver.hpp>` and `int main() {}` with no `-I` to libmicrohttpd / pthread / gnutls headers.
2. **Build-flag invariance** (PRD-FLG-REQ-001): the same consumer source compiles against `--disable-tls` and `--enable-tls` builds without changes.
3. **Move semantics on `http_response`** (DR-5): sanitizer-clean tests for inline↔inline, inline↔heap, heap↔inline, heap↔heap on both move-construct and move-assign. The sanitizer matrix covers asan, lsan, tsan, ubsan, and msan. **MemorySanitizer (msan) coverage is deliberately scoped to the backend-free unit-test subset** (`unit/` targets in `test/Makefile.am`) because curl — used by all integration round-trips via `test/integ/curl_helpers.hpp` — cannot be practically instrumented; uninstrumented curl output would surface as false `use-of-uninitialized-value` reports. libc++ and libmicrohttpd are built instrumented in-CI (TASK-087) to keep the unit subset clean without wildcard suppressions.
4. **SBO size invariant** (DR-5): `static_assert(sizeof(detail::deferred_body) <= http_response::body_buf_size, ...)` at the end of `detail/body.hpp`. Compile-time guarantee.
5. **Routing semantics preservation** (DR-7): the v1 routing-test corpus runs against v2.0 unchanged. Any regression is treated as a release-blocker.
6. **Thread-safety contract** (DR-8): a stress test exercises `register_path` / `unregister_path` / `block_ip` / `unblock_ip` from within handlers, verifies no deadlock except for the documented `stop()` case. An opt-in negative test (`stop_from_handler_deadlocks_as_documented`, enabled via `HTTPSERVER_RUN_STOP_FROM_HANDLER=1`) confirms the deadlock contract by forking a child that calls `stop()` from inside a handler.
7. **Performance acceptance gates** (PRD §3.6 / TASK-039): `make bench` verifies two numeric criteria:
   - `get_headers()` warm-path cost ≥10× faster than v1 (PRD-REQ-REQ-001); checked by `test/bench_get_headers.cpp` at runtime.
   - `sizeof(http_resource)` shrinks by at least the empty `std::map<std::string,bool>` footprint (PRD-REQ-REQ-003); checked by `test/bench_sizeof_http_resource.cpp` at compile time.
   These benchmarks live in `EXTRA_PROGRAMS` (not `check_PROGRAMS`) so they do not run under sanitizer CI; they are gated on a quiet release-mode host as part of the release runbook.

---
