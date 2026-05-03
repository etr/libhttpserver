## 9) Testing strategy

The architecture itself does not prescribe test frameworks (out of architecture scope), but it does name the test surfaces that need first-class coverage given v2.0's structural changes:

1. **Header hygiene** (PRD-HDR-REQ-001..003): a CI test compiles a TU containing only `#include <httpserver.hpp>` and `int main() {}` with no `-I` to libmicrohttpd / pthread / gnutls headers.
2. **Build-flag invariance** (PRD-FLG-REQ-001): the same consumer source compiles against `--disable-tls` and `--enable-tls` builds without changes.
3. **Move semantics on `http_response`** (DR-5): sanitizer-clean tests for inline↔inline, inline↔heap, heap↔inline, heap↔heap on both move-construct and move-assign.
4. **SBO size invariant** (DR-5): `static_assert(sizeof(detail::deferred_body) <= http_response::body_buf_size, ...)` at the end of `detail/body.hpp`. Compile-time guarantee.
5. **Routing semantics preservation** (DR-7): the v1 routing-test corpus runs against v2.0 unchanged. Any regression is treated as a release-blocker.
6. **Thread-safety contract** (DR-8): a stress test exercises `register_resource` / `block_ip` from within handlers, verifies no deadlock except for the documented `stop()` case.

---
