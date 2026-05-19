## 2) Architectural Drivers

### 2.1 Business Drivers (from PRD §1)
- **Vision:** A modern, ergonomic C++ HTTP server library that hides its libmicrohttpd backend, fits 2026 C++ idioms, and is safe to use without reading the source.
- **JTBD: 30-line endpoint without subclassing.** Drives the lambda-first handler model and value-typed response.
- **JTBD: Build flags must not leak.** Drives the build-flag-independent ABI and unconditional declarations.
- **JTBD: No transitive C-header inclusion.** Drives PIMPL and forward declarations on backend types.
- **North-star: hello world ≤10 LOC**, zero public-header dependencies on backend C types.

### 2.2 Quality Attributes (from PRD §2)

| Attribute | Requirement | Architecture response |
|---|---|---|
| Public-header decoupling | No `<microhttpd.h>` / `<gnutls/gnutls.h>` / `<pthread.h>` / `<sys/socket.h>` / `<sys/uio.h>` in installed headers | PIMPL on `webserver` and `http_request`; forward-declared `detail::body` for `http_response`; high-level accessors (cert DN, fingerprint) replacing raw GnuTLS handles; library-defined `httpserver::iovec_entry` POD replacing `struct iovec` in the public `http_response::iovec(...)` factory |
| Build-flag stability | Public API surface invariant under `HAVE_BAUTH` / `HAVE_DAUTH` / `HAVE_GNUTLS` / `HAVE_WEBSOCKET` | Unconditional declarations; runtime sentinels or `feature_unavailable` throws when backends disabled; `webserver::features()` reports availability |
| Const correctness | Pure accessors `const`; lazy caches OK via `mutable`; daemon-driving methods exempt | Request-side caches in `mutable` storage (or unique_ptr); `is_running` / `get_fdset` / `get_timeout` documented as exempt operations |
| Hot-path performance | Per-request getters do not allocate or copy containers | Container-returning getters change to `const&` / `string_view`; per-request impl arena-allocated from a per-connection `std::pmr::monotonic_buffer_resource`; method-state held as a `uint32_t` bitmask, not a `std::map` |
| Naming | Snake_case + one canonical verb per concept | `block_ip` / `unblock_ip` (replacing four ban/allow synonyms); `_handler` suffix (replacing `_resource` for function-shaped setters); `shoutCAST` grandfathered as a protocol identifier |
| Documentation | v2.0 ships rewritten README, examples, RELEASE_NOTES.md | Out of architecture scope; flagged in §13 as a documentation-track deliverable |

### 2.3 Constraints

**Technical:**
- libmicrohttpd is the only backend; pluggable backends are explicitly out of scope (PRD §3.1).
- Distro packagers are a named target user segment (PRD §1) — system-toolchain compatibility on Debian stable, RHEL, FreeBSD ports must be respected.
- The library is currently autoconf-built; v2.0 keeps that toolchain.

**Team:**
- Single maintainer (Sebastiano Merlino) plus drive-by contributors. Architecture choices favor maintainability over novelty.

**Release:**
- v2.0 is a hard cutover. No v1.x maintenance branch. SOVERSION bump (PRD §1, OQ-007).

---
