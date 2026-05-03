# EARS-based Product Requirements

**Doc status:** Draft 0.4
**Last updated:** 2026-04-30
**Owner:** Sebastiano Merlino
**Audience:** Maintainers, library consumers, distro packagers

---

## 0) How we'll write requirements (EARS cheat sheet)
- **Ubiquitous form:** "When <trigger> then the system shall <response>."
- **Optional elements:** [when/while/until/where] <trigger>, the system shall <response>.
- **Style:** Clear, atomic, testable, technology-agnostic.

---

## 1) Product context
- **Vision:** A modern, ergonomic C++ HTTP server library that hides its libmicrohttpd backend, fits 2026 C++ idioms, and is safe to use without reading the source.
- **Target users / segments:** C++ developers embedding an HTTP server (services, tools, test fixtures); distro packagers; downstream library authors.
- **Key JTBDs:**
  - "Add an HTTP endpoint to my service in under 30 lines without subclassing."
  - "Compile against the library without my code mysteriously failing because of a build flag."
  - "Avoid forcing my callers to transitively pull in `<microhttpd.h>` and `<pthread.h>`."
- **North-star metrics:**
  - Public-header dependencies on backend C types: 0.
  - Paired `foo()/no_foo()` setters: 0.
  - Hello-world example LOC: ≤10 (currently ~15 with subclassing).
- **Release strategy:** Single breaking release as **v2.0** with a SOVERSION bump. No deprecation period, no compatibility shims, no migration macro. v2.0 is a clean cutover — the v1.x line is end-of-life on the day v2.0 ships; there is no parallel maintenance branch.

---

## 2) Non‑functional & cross‑cutting requirements
- **Build-time stability:** Public API surface shall not vary based on build-time feature flags (`HAVE_BAUTH`, `HAVE_DAUTH`, `HAVE_GNUTLS`, `HAVE_WEBSOCKET`).
- **Header hygiene:** Public headers shall not include `<microhttpd.h>`, `<gnutls/gnutls.h>`, `<pthread.h>`, or `<sys/socket.h>`.
- **Const correctness:** Pure accessors of object state shall be `const`. Logical-const lazy caching (e.g. populating a request-scoped cache on first call) is permitted and shall be implemented via `mutable` storage or equivalent indirection. Methods that drive or query external mutable state — the libmicrohttpd daemon, OS sockets, the listening event loop — are not subject to this rule even when named `get_*` (e.g. `webserver::is_running`, `get_fdset`, `get_timeout`, `add_connection`).
- **Hot-path performance:** Per-request getters shall not allocate or copy containers; they return `const&` or `string_view`.
- **Naming:** All public method names shall be snake_case; one canonical verb per concept.
- **Documentation:** v2.0 ships with a rewritten `README` and an updated examples set. A short `RELEASE_NOTES.md` summarizes the API changes for users porting from v1; it is informational, not a compatibility commitment.

---

## 3) Feature list (living backlog)

### 3.1 Public Header Decoupling (API-HDR)

**Problem / outcome**
Public headers leak the libmicrohttpd C backend (`MHD_Connection*`, `MHD_Response*`, `microhttpd.h`), `<pthread.h>`, and `<gnutls/gnutls.h>` into every consumer translation unit. This makes the C dependency mandatory for users, slows compile times, and prevents future backend swaps. After this work, consumers can `#include <httpserver.hpp>` and see only C++ types declared by libhttpserver.

**In scope**
- Use the PIMPL idiom for `webserver`, `http_request`, and `http_response`: backend state (`MHD_Daemon*`, `MHD_Connection*`, `MHD_Response*`, mutexes, GnuTLS handles) lives in an `impl` struct defined in a private header. Public headers carry only a `std::unique_ptr<impl>`. Cost: one extra heap allocation per object on the relevant hot paths; benefit: the public ABI no longer leaks any backend type.
- Move `get_raw_response` / `decorate_response` / `enqueue_response` virtuals off the public `http_response` (relocate to a detail base or eliminate).
- Remove `microhttpd.h`, `pthread.h`, `<sys/socket.h>` includes from public headers.
- Replace `gnutls_session_t`-returning methods on `http_request` with high-level accessors (cert DN, fingerprint, etc.) or an opaque handle.

**Out of scope**
- Replacing libmicrohttpd as the backend.
- Pluggable backends.

**EARS Requirements**
- `PRD-HDR-REQ-001` When a consumer includes `<httpserver.hpp>` then the system shall not transitively include `<microhttpd.h>`.
- `PRD-HDR-REQ-002` When a consumer includes `<httpserver.hpp>` then the system shall not transitively include `<pthread.h>` or `<sys/socket.h>`.
- `PRD-HDR-REQ-003` When a consumer includes `<httpserver.hpp>` then the system shall not transitively include `<gnutls/gnutls.h>`.
- `PRD-HDR-REQ-004` Where a public class needs to hold backend state then the system shall hold it via PIMPL (`std::unique_ptr<impl>`) whose `impl` definition lives in a private header. `http_response` is exempt: it does not hold backend state (the `MHD_Response*` is created from the response value inside the dispatch path, never carried on the public type), so it remains a non-PIMPL value type.
- `PRD-HDR-REQ-005` When `get_raw_response`, `decorate_response`, or `enqueue_response` are referenced by user code then the system shall not provide them as part of the public API.

**Acceptance criteria**
- `grep -lE 'microhttpd\.h|pthread\.h|gnutls\.h|sys/socket\.h' src/httpserver/*.hpp` returns no results.
- A test program containing only `#include <httpserver.hpp>` and an empty `main()` compiles without `-I` to libmicrohttpd headers.

---

### 3.2 Build-Flag-Independent Public API (API-FLG)

**Problem / outcome**
Methods like `get_user`, `get_pass`, `get_digested_user`, `check_digest_auth`, the `get_client_cert_*` family, and `basic_auth()` on the builder are gated behind `#ifdef HAVE_BAUTH`/`HAVE_DAUTH`/`HAVE_GNUTLS`/`HAVE_WEBSOCKET`. Users must mirror the library's build flags or get inscrutable errors. After this work, declarations are stable across configurations; missing features are reported at runtime.

**In scope**
- Remove `#ifdef HAVE_*` guards from public headers.
- When a backend is disabled at build time, methods return a documented sentinel (empty `string_view`, `false`, etc.) or throw `httpserver::feature_unavailable`. `feature_unavailable` derives from `std::runtime_error`.
- Add `webserver::features()` returning a `struct` of `bool` flags (`basic_auth`, `digest_auth`, `tls`, `websocket`). The struct form is preferred over a bitmask or `std::set<std::string>` because individual fields are discoverable via auto-completion and stable to extend.
- Library build configuration remains unchanged (Autoconf can still disable backend code paths).

**Out of scope**
- Forcing all backends to be present at runtime.

**EARS Requirements**
- `PRD-FLG-REQ-001` When a public header is parsed then the system shall not gate any declaration on `HAVE_BAUTH`, `HAVE_DAUTH`, `HAVE_GNUTLS`, or `HAVE_WEBSOCKET`.
- `PRD-FLG-REQ-002` When a user calls a feature method whose backend was disabled at build time then the system shall return a documented sentinel value or throw `httpserver::feature_unavailable`.
- `PRD-FLG-REQ-003` When a user calls `webserver::features()` then the system shall return a `struct` of `bool` fields reporting the runtime availability of basic-auth, digest-auth, TLS, and websockets.
- `PRD-FLG-REQ-004` If a feature is unavailable and the user invokes it then the error message shall name both the feature and the build flag that controls it.
- `PRD-FLG-REQ-005` When the system defines `httpserver::feature_unavailable` then it shall publicly inherit from `std::runtime_error`.

**Acceptance criteria**
- `grep -E '#if(def)? HAVE_(BAUTH|DAUTH|GNUTLS|WEBSOCKET)' src/httpserver/*.hpp` returns no results.
- A consumer compiles the same source against two builds (TLS-on, TLS-off) without source changes.

---

### 3.3 Configuration Builder Cleanup (API-CFG)

**Problem / outcome**
`create_webserver` has 70+ setters with paired `foo()`/`no_foo()` for nearly every boolean (`use_ssl`/`no_ssl`, `debug`/`no_debug`, `pedantic`/`no_pedantic`, `basic_auth`/`no_basic_auth`, `digest_auth`/`no_digest_auth`, `deferred`/`no_deferred`, `regex_checking`/`no_regex_checking`, `ban_system`/`no_ban_system`, `post_process`/`no_post_process`, `single_resource`/`no_single_resource`, `use_ipv6`/`no_ipv6`, `use_dual_stack`/`no_dual_stack`, etc.) — doubling the surface for zero expressive gain. Constants like `DEFAULT_WS_PORT`, `DEFAULT_WS_TIMEOUT`, `NOT_FOUND_ERROR` are exposed as `#define` macros polluting consumer namespaces. After this work the builder is roughly half its current size, accepts `bool` arguments, and exposes constants as `constexpr`.

**In scope**
- Replace each paired `foo()/no_foo()` with a single `foo(bool = true)` setter.
- Replace `#define` constants in public headers with `constexpr` in `httpserver::constants`.
- Validate setter inputs at the build step (port range, non-negative thread counts, etc.) and throw on misuse.

**Out of scope**
- Replacing the builder pattern with a config struct.

**EARS Requirements**
- `PRD-CFG-REQ-001` When a user calls a boolean configuration setter then the system shall accept a `bool` argument with default `true`.
- `PRD-CFG-REQ-002` When a public header defines a constant then the system shall use `constexpr` inside the `httpserver` namespace, not `#define`.
- `PRD-CFG-REQ-003` If a setter receives an out-of-range value (port > 65535, negative threads, etc.) then the system shall throw `std::invalid_argument` with a descriptive message.
- `PRD-CFG-REQ-004` When v2.0 ships then `no_foo()` setters shall not exist in the public API.

**Acceptance criteria**
- `create_webserver.hpp` line count reduced by ≥30%.
- `grep -E '^\s*create_webserver& no_' src/httpserver/create_webserver.hpp` returns 0.
- `grep -E '^#define\s' src/httpserver/*.hpp` returns 0.

---

### 3.4 Handler Model and Ownership (API-HDL)

**Problem / outcome**
Today, even the simplest stateless handler forces the user to subclass `http_resource`, override one of nine `render_*` virtuals, and pass a raw pointer whose lifetime they manage. The class form is the right shape when state is *shared across HTTP methods of the same resource* — a per-resource counter, cache, DB handle, or auth context that `GET` reads and `POST` mutates. It is overkill for a handler that is stateless or whose state is fixed at construction. There is also a parallel function-handler convention (`render_ptr`) used for not-found / error / auth handlers — two styles for one job. `register_resource` further has an opaque `bool family` parameter for prefix matching. After this work, both registration styles are first-class: lambdas for stateless or capture-stateful handlers, `http_resource` subclasses for shared mutable state — picked by the shape of the problem, not forced by the API. Smart-pointer ownership replaces the raw pointer.

**In scope**
- Add `webserver::on_get/on_post/on_put/on_delete/on_patch/on_options/on_head` overloads taking `std::function<http_response(const http_request&)>` (handler returns `http_response` by value; the library moves the returned value into the dispatch path).
- Add a generic `webserver::route(http_method, path, handler)` taking the same handler signature, for table-driven registration where the method is a runtime value (config-loaded route tables, programmatic registration). The method-specific `on_*` entry points remain the preferred call-site form; `route` is the escape hatch for when the method isn't known statically.
- `register_resource` takes `std::unique_ptr<http_resource>` (move-in ownership) or `std::shared_ptr<http_resource>`. The raw-pointer overload is removed.
- Replace the `bool family` parameter with named methods (`register_prefix` vs `register_path`).
- Update examples: lambda-first for the stateless "hello world" path, a class-based example explicitly demonstrating state shared across `GET`/`POST` on the same resource.

**Out of scope**
- Removing the inheritance-based API. Subclassing `http_resource` remains the canonical way to share mutable state across HTTP methods of one resource.

**EARS Requirements**
- `PRD-HDL-REQ-001` When a user registers a handler then the system shall accept a `std::function<http_response(const http_request&)>` overload — the handler returns `http_response` by value.
- `PRD-HDL-REQ-002` When a user wants to register a method-specific handler then the system shall provide entry points named `on_get`, `on_post`, `on_put`, `on_delete`, `on_patch`, `on_options`, `on_head`.
- `PRD-HDL-REQ-006` When a user wants to register a handler with the HTTP method known only at runtime then the system shall provide a generic `webserver::route(http_method, const std::string& path, handler)` entry point taking the same `http_response`-by-value handler signature as `on_get` etc.
- `PRD-HDL-REQ-003` When a user passes ownership of an `http_resource` or a `websocket_handler` then the system shall accept `std::unique_ptr` and `std::shared_ptr` overloads of `register_resource` and `register_ws_resource` respectively.
- `PRD-HDL-REQ-004` When a user wants prefix matching then the system shall expose `register_prefix(...)` instead of a positional `bool family` parameter.
- `PRD-HDL-REQ-005` When v2.0 ships then the raw-pointer overloads `register_resource(string, http_resource*, bool)` and `register_ws_resource(string, websocket_handler*)` shall not exist in the public API.

**Acceptance criteria**
- A "hello world" example compiles with no subclass, no raw pointers, in ≤10 lines including `main()`.

---

### 3.5 Response Model Simplification (API-RSP)

**Problem / outcome**
The response hierarchy has eight subclasses (`string_response`, `file_response`, `iovec_response`, `pipe_response`, `deferred_response`, `empty_response`, `basic_auth_fail_response`, `digest_auth_fail_response`). `http_response` itself uses `shared_ptr` returns when there is no shared ownership, exposes mutable getters that aren't `const` (`get_header` calls `headers[key]` and inserts on miss), and `with_header`/`with_footer`/`with_cookie` look fluent but return `void`. Cookies and headers are stored in separate maps despite cookies being headers. After this work `http_response` is a value type with factory functions, `const`-correct getters, and a true fluent `with_*` chain.

**In scope**
- `http_response` is a sealed value type built via factory functions: `http_response::string(...)`, `http_response::file(...)`, `http_response::iovec(...)`, `http_response::pipe(...)`, `http_response::empty(...)`, `http_response::deferred(...)`, `http_response::unauthorized(scheme, realm, ...)`.
- Remove the `*_response` subclasses entirely.
- `with_header`/`with_footer`/`with_cookie` return `http_response&`.
- `get_header`/`get_footer`/`get_cookie` are `const`, return `string_view`, do not insert on miss.
- Handler return type is `http_response` by value. The library moves the response into the dispatch path; no `unique_ptr` or `shared_ptr` wrapping is required.

**Out of scope**
- Changing how deferred/streaming responses work internally.

**EARS Requirements**
- `PRD-RSP-REQ-001` When a user constructs a response then the system shall provide a factory function returning `http_response` by value.
- `PRD-RSP-REQ-002` When a user calls `get_header`, `get_footer`, or `get_cookie` then the system shall not modify the response object's state.
- `PRD-RSP-REQ-003` When a user calls `get_header` on a missing key then the system shall return an empty `string_view`, not insert a new entry.
- `PRD-RSP-REQ-004` When a user calls `with_header`, `with_footer`, or `with_cookie` then the system shall return a reference to `*this` to support chaining.
- `PRD-RSP-REQ-005` When a user wants to send an authentication failure then the system shall expose `http_response::unauthorized(scheme, realm, …)`.
- `PRD-RSP-REQ-006` When v2.0 ships then `string_response`, `file_response`, `iovec_response`, `pipe_response`, `deferred_response`, `empty_response`, `basic_auth_fail_response`, and `digest_auth_fail_response` shall not exist in the public API.
- `PRD-RSP-REQ-007` When a user returns a response from a handler then the system shall accept `http_response` by value, with the library moving the value into the dispatch path. Neither `std::unique_ptr<http_response>` nor `std::shared_ptr<http_response>` shall be required.

**Acceptance criteria**
- `get_header` is callable on `const http_response&`.
- `auto r = http_response::string("hi").with_header("X-Foo", "bar").with_status(201);` compiles and chains.
- `grep -E 'class\s+\w+_response\s*:' src/httpserver/*.hpp` returns no public results.

---

### 3.6 Request Type Ergonomics (API-REQ)

**Problem / outcome**
`http_request::get_args`, `get_path_pieces`, `get_files`, `get_headers` return whole maps/vectors by value (some nested). `http_resource::is_allowed` and `get_allowed_methods` are non-`const` despite only reading state. Each `http_resource` instance allocates a `std::map<std::string, bool>` of HTTP methods on construction. After this work, hot-path getters return `const&` or `string_view`, read methods are `const`, and method state is a fixed-size bitmask.

**In scope**
- Change container-returning getters on `http_request` to return `const ContainerType&`.
- Make `is_allowed`, `get_allowed_methods` `const`.
- Replace `method_state` map with a bitmask over an HTTP-method enum.
- Audit `string_view` returns for dangling-view risk and document lifetime guarantees.

**Out of scope**
- Changing the move-only identity of `http_request`.

**EARS Requirements**
- `PRD-REQ-REQ-001` When a user calls `get_args`, `get_path_pieces`, `get_files`, or `get_headers` on `http_request` then the system shall return a `const&` to internal storage.
- `PRD-REQ-REQ-002` When a user calls `is_allowed` or `get_allowed_methods` on `http_resource` then the method shall be `const`.
- `PRD-REQ-REQ-003` When a method's allow/disallow state is queried then the system shall use a fixed-size bitmask over an HTTP-method enum, not a `std::map<std::string, bool>`.

**Acceptance criteria**
- A microbenchmark of `req.get_headers()` shows ≥10× reduction in per-call cost vs v1.
- `sizeof(http_resource)` decreases by at least the cost of an empty `std::map`.

---

### 3.7 Naming and Verb Consistency (API-NAM)

**Problem / outcome**
`stop()` vs `sweet_kill()` (two terminate verbs); `ban_ip`/`disallow_ip`/`allow_ip`/`unban_ip` (four verbs, two concepts); `register_resource` (object) vs `not_found_resource` (function) using "resource" for two distinct things; the `webserver(const create_webserver&)` constructor is `// NOLINT(runtime/explicit)` non-explicit, allowing surprising implicit conversions. After this work the public API uses one canonical verb per concept and snake_case throughout, with one historical exception: `shoutCAST()` is preserved as-is — the name is a deliberate nod to the SHOUTcast streaming protocol it implements, and renaming it would obscure that mapping. It is grandfathered into the public API.

**In scope**
- Rename `sweet_kill` → `stop_and_wait`.
- Collapse the ban/allow verbs to the network-flavored pair `block_ip` / `unblock_ip`. Drop `ban_ip`, `unban_ip`, `allow_ip`, `disallow_ip`.
- Rename `not_found_resource`/`method_not_allowed_resource`/`internal_error_resource` setters to `not_found_handler`/`method_not_allowed_handler`/`internal_error_handler`.
- Make the `webserver(const create_webserver&)` constructor `explicit`.

**Out of scope**
- Renaming top-level types (`webserver`, `http_request`, `http_response`, `http_resource`).
- Renaming `shoutCAST` (preserved as protocol name; see Problem / outcome).

**EARS Requirements**
- `PRD-NAM-REQ-001` When a user inspects the public API then the system shall use snake_case for all method names, except `shoutCAST` which is preserved as a protocol identifier.
- `PRD-NAM-REQ-002` When two methods would denote the same concept then the system shall provide exactly one canonical name.
- `PRD-NAM-REQ-003` When a function-based handler setter is named then the system shall use the suffix `_handler` (not `_resource`).
- `PRD-NAM-REQ-004` When a user constructs a `webserver` from a `create_webserver` then the conversion shall be `explicit`.
- `PRD-NAM-REQ-005` When the system exposes IP access-control verbs then it shall provide exactly the pair `block_ip` / `unblock_ip` and shall not expose `ban_ip`, `unban_ip`, `allow_ip`, or `disallow_ip`.

**Acceptance criteria**
- `grep -E '[a-z][A-Z]' src/httpserver/*.hpp` returns no public method names matching camelCase other than `shoutCAST`.
- For each pair of synonymous verbs in v1 (`sweet_kill`/`stop`, `ban_ip`/`disallow_ip`, `allow_ip`/`unban_ip`), only the canonical name survives in v2.0.

---

## 4) Traceability
- API-HDR → `src/httpserver/*.hpp`, `src/webserver.cpp`, `src/http_response.cpp`
- API-FLG → `src/httpserver/*.hpp`, `src/webserver.cpp`, `src/http_request.cpp`
- API-CFG → `src/httpserver/create_webserver.hpp`, `src/httpserver/webserver.hpp`
- API-HDL → `src/httpserver/webserver.hpp`, `src/httpserver/http_resource.hpp`, `examples/`
- API-RSP → `src/httpserver/http_response.hpp`, `src/httpserver/*_response.hpp`
- API-REQ → `src/httpserver/http_request.hpp`, `src/httpserver/http_resource.hpp`
- API-NAM → `src/httpserver/webserver.hpp`, `src/httpserver/http_response.hpp`, `README.md`

---

## 5) Open questions log

### Resolved
- **OQ-001 — `features()` shape.** Resolved 2026-04-30: `struct` of `bool`s. Discoverable via auto-completion, easy to extend without breaking ABI. Folded into 3.2.
- **OQ-002 — PIMPL vs forward declarations.** Resolved 2026-04-30: full PIMPL on `webserver`, `http_request`, `http_response`. Accepting one heap allocation per object as the cost of buying a clean, backend-agnostic public ABI. Folded into 3.1.
- **OQ-004 — ban/allow verb collapse.** Resolved 2026-04-30: `block_ip` / `unblock_ip`. Network-flavored, symmetric, no existing-API inertia worth preserving. Folded into 3.7.
- **OQ-005 — drop `shoutCAST`?** Resolved 2026-04-30: keep `shoutCAST` as-is. The name maps to the SHOUTcast streaming protocol it implements; renaming to `shoutcast` would obscure that. Grandfathered as the only camelCase identifier in the public API. Folded into 3.7.
- **OQ-006 — `feature_unavailable` base class.** Resolved 2026-04-30: derives from `std::runtime_error`. Standard, integrates with existing exception-handling code, no need for a library-specific base. Folded into 3.2.
- **OQ-007 — v1.x maintenance branch?** Resolved 2026-04-30: no maintenance branch. v2.0 is a hard cutover; v1.x is end-of-life on the day v2.0 ships. Folded into §1.

### Resolved (cont.)
- **OQ-003 — generic `route(method, path, handler)` alongside `on_get`/`on_post`/...?** Resolved 2026-04-30: ship both. `on_*` is the preferred call-site form (clearer when the method is known statically); `route` is the escape hatch for table-driven registration where the method is a runtime value. The cost of carrying one extra entry point is small; the cost of forcing every table-driven user to write a 7-arm `switch` is paid forever. Folded into 3.4.

### Open
*(none)*
