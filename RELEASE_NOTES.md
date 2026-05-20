# libhttpserver v2.0 — Release Notes

> **Status:** Informational, not a compatibility commitment.
> This document summarises every public-surface change in v2.0 to help v1
> consumers port. It is not part of the API contract and is not exhaustive
> — when in doubt, the headers in `src/httpserver/` are authoritative.

## TL;DR

libhttpserver v2.0 is a **clean cutover** from the v1.x line: no shims, no
deprecated overloads, no inline namespace. Handlers are now lambdas
(`ws.on_get("/", [](auto&){ return http_response::string("hi"); })`);
responses are value types built from `http_response::string` / `file` /
`iovec` / `pipe` / `empty` / `deferred` / `unauthorized` factories;
request getters return `const&` / `string_view` and never insert on miss;
the threading and error-propagation contracts are documented (DR-008,
DR-009); features disabled at build time report at runtime via
`feature_unavailable` or a documented sentinel value instead of vanishing
from the API. SOVERSION bumps from 1 to 2 and v2 ships as a
`libhttpserver2` binary that is parallel-installable with `libhttpserver1`.
v1.x is end-of-life on the day v2.0 ships.

## What's gone

- **Response subclasses.** `string_response`, `file_response`,
  `iovec_response`, `pipe_response`, `deferred_response`, `empty_response`,
  `basic_auth_fail_response`, `digest_auth_fail_response` are removed.
  Build responses through the `http_response::*` factory chain instead
  (see "What's renamed" for the 1:1 mapping).
- **Raw-pointer registration.** `register_resource(string, http_resource*, bool family)`
  is gone. Use `register_path(unique_ptr|shared_ptr)` or
  `register_prefix(unique_ptr|shared_ptr)`.
  `register_ws_resource(string, websocket_handler*)` is gone. Use the
  smart-pointer overload.
- **`sweet_kill`.** Removed. Use `stop_and_wait()` (or `stop()` for the
  signal-only form).
- **IP allow/deny verbs.** `ban_ip`, `unban_ip`, `allow_ip`,
  `disallow_ip` are removed. Use `block_ip` / `unblock_ip`.
- **Paired `no_*` boolean setters.** `no_basic_auth`, `no_digest_auth`,
  `no_ssl`, `no_debug`, `no_pedantic`, `no_deferred`, `no_regex_checking`,
  `no_ban_system`, `no_post_process`, `no_single_resource`, `no_ipv6`,
  `no_dual_stack` are removed. The remaining single setter takes a `bool`:
  e.g. `cw.ssl(false)` replaces `cw.no_ssl()`.
- **`#define` constants in public headers.** `DEFAULT_WS_PORT`,
  `DEFAULT_WS_TIMEOUT`, and the `*_ERROR` / `*_RESPONSE` macros are gone.
  Their `constexpr` replacements live in the `httpserver::constants`
  namespace.
- **`gnutls_session_t`-returning methods on `http_request`.** Removed.
  Use the high-level accessors: `get_client_cert_dn()`,
  `get_client_cert_issuer_dn()`, `get_client_cert_fingerprint()`,
  `get_cipher_suite()`, `get_protocol_version()`, `get_client_kx()`,
  `get_client_cert_type()`, `get_client_credential_type()`.
- **Public virtuals on `http_response`.** `get_raw_response`,
  `decorate_response`, `enqueue_response` are gone. `http_response` is no
  longer an open base class; it is a value type with no virtual methods.
- **`#ifdef HAVE_BAUTH` / `HAVE_DAUTH` / `HAVE_GNUTLS` / `HAVE_WEBSOCKET`
  guards in public headers.** Removed. The public API is now
  build-flag-invariant: every method exists on every build. Calls into a
  feature disabled at configure time throw `feature_unavailable` or
  return a documented sentinel; query the runtime configuration via
  `ws.features()`.
- **Public transitive includes of backend C headers.** `<microhttpd.h>`,
  `<pthread.h>`, `<gnutls/gnutls.h>`, `<sys/socket.h>` no longer leak
  through `<httpserver.hpp>`. A consumer that needs MHD or GnuTLS types
  must include them directly.
- **The implicit conversion** `webserver ws = cw;` (where `cw` is a
  `create_webserver`). The constructor is now `explicit`.

## What's new

- **Lambda registration.** `ws.on_get(path, handler)`, `on_post`,
  `on_put`, `on_delete`, `on_patch`, `on_options`, `on_head`. The handler
  is `http_response(const http_request&)`. No subclass required.
- **Runtime-method registration.** `ws.route(http_method | method_set,
  path, handler)` registers a single handler for a method known only at
  runtime, or atomically for a bitmask of methods.
- **Smart-pointer resource registration.** `register_path(path, h)` and
  `register_prefix(path, h)` accept `std::unique_ptr<http_resource>` or
  `std::shared_ptr<http_resource>`. The boolean `family` flag that used
  to live as the third positional argument of `register_resource` is gone
  — the choice is encoded in the method name.
- **`http_response` factory chain.** `http_response::string(body)`,
  `http_response::file(path)`, `http_response::iovec(entries)`,
  `http_response::pipe(fd)`, `http_response::empty(status)`,
  `http_response::deferred(...)`,
  `http_response::unauthorized(scheme, realm, ...)`. Each returns a
  value-type `http_response`. Chain fluent mutators:
  `.with_status(int)`, `.with_header(name, value)`, `.with_footer(...)`,
  `.with_cookie(...)`.
- **`feature_unavailable`.** Public `std::runtime_error` subclass thrown
  by API entry points whose backend feature was disabled at configure
  time (e.g. calling `ssl_cert(...)` on a `HAVE_GNUTLS=no` build).
- **`webserver::features()`.** Returns a struct of `bool`s describing
  which optional features are linked in (TLS, basic auth, digest auth,
  websocket, …). Use this for runtime branching instead of `#ifdef HAVE_*`.
- **`iovec_entry`.** Public POD struct (`{void* iov_base; size_t iov_len;}`)
  used by `http_response::iovec`. Replaces the opaque scatter-gather
  type that v1 leaked from `<sys/uio.h>`.
- **`http_method` / `method_set`.** Strongly-typed enum and bitmask for
  HTTP methods. `route()` accepts either; `http_resource::allow_methods`
  uses `method_set`.
- **`httpserver::constants` namespace.** `constexpr` replacements for
  every removed `#define`.

## What's renamed (v1 → v2)

Every entry below is a single line so a v1 user can grep for the v1 name
and see the v2 replacement.

| v1 | v2 |
|---|---|
| `sweet_kill` | `stop_and_wait` |
| `ban_ip` | `block_ip` |
| `unban_ip` | `unblock_ip` |
| `allow_ip` | `unblock_ip` (or `block_ip` to deny) |
| `disallow_ip` | `block_ip` (or `unblock_ip` to allow) |
| `not_found_resource` (setter) | `not_found_handler` |
| `method_not_allowed_resource` (setter) | `method_not_allowed_handler` |
| `internal_error_resource` (setter) | `internal_error_handler` |
| `render_GET` | `render_get` |
| `render_POST` | `render_post` |
| `render_PUT` | `render_put` |
| `render_DELETE` | `render_delete` |
| `render_HEAD` | `render_head` |
| `render_OPTIONS` | `render_options` |
| `render_PATCH` | `render_patch` |
| `render_CONNECT` | `render_connect` |
| `render_TRACE` | `render_trace` |
| `webserver(create_webserver const&)` — implicit | `explicit webserver(create_webserver const&)` |
| `register_resource(string, http_resource*, bool family)` — raw pointer + bool flag | `register_path` / `register_prefix` taking `unique_ptr` or `shared_ptr` |
| `register_ws_resource(string, websocket_handler*)` — raw pointer | `register_ws_resource(string, unique_ptr<websocket_handler>)` (and a `shared_ptr` overload) |
| `string_response` | `http_response::string` |
| `file_response` | `http_response::file` |
| `iovec_response` | `http_response::iovec` |
| `pipe_response` | `http_response::pipe` |
| `empty_response` | `http_response::empty` |
| `deferred_response` | `http_response::deferred` |
| `basic_auth_fail_response` | `http_response::unauthorized` |
| `digest_auth_fail_response` | `http_response::unauthorized` |

## What changed semantically

- **Handlers return `http_response` by value.** v1 returned
  `std::unique_ptr<http_response>` (and earlier `std::shared_ptr<http_response>`);
  v2 returns a value. The framework moves it into the dispatch path. No
  heap allocation is required for small responses (small-buffer optimisation
  inside `http_response`).
- **`http_request` getters return `const&` / `string_view`.** v1's
  `get_header(name)` (and `get_arg`, `get_cookie`, `get_footer`) returned
  by value and inserted an empty entry into the request map on miss; v2's
  versions return a reference / `string_view` and never mutate the
  request. Container getters (`get_headers()`, `get_args()`, …) return
  `const&` to the underlying map.
- **`http_response::get_header` / `get_footer` / `get_cookie` are
  const and do not insert on miss.** Mutation uses the
  `with_header` / `with_footer` / `with_cookie` chain, which returns
  `*this` by reference (`&` on lvalue calls, `&&` on rvalue calls) for
  fluent assembly.
- **`webserver(create_webserver const&)` is `explicit`.** Direct-init
  `webserver ws{cw};` rather than copy-init `webserver ws = cw;`.

## Threading

The threading contract is now documented; in v1 it was implicit.
See [README.md "Threading contract"](README.md), architecture §5.1
([specs/architecture/05-cross-cutting.md](specs/architecture/05-cross-cutting.md))
and DR-008 ([specs/architecture/11-decisions/DR-008.md](specs/architecture/11-decisions/DR-008.md))
for the full statement. The load-bearing points for porters:

- Handler invocations from MHD worker threads run concurrently. Shared
  state owned by an `http_resource` subclass (or captured by a lambda)
  must be synchronised by the application.
- `start()` is one-shot; **do not** call `stop()` or `stop_and_wait()`
  from inside a handler thread — MHD's joinable internal thread cannot
  join itself and the call aborts the process (DR-008).
- `~webserver()` runs `stop_and_wait()` if the server is still running,
  so the same self-join restriction applies to destruction from a
  handler thread.

## Error propagation

The error-propagation contract is now documented; in v1 it was implicit.
See [README.md "Error propagation"](README.md), architecture §5.2
([specs/architecture/05-cross-cutting.md](specs/architecture/05-cross-cutting.md))
and DR-009 ([specs/architecture/11-decisions/DR-009.md](specs/architecture/11-decisions/DR-009.md))
for the full statement. The load-bearing points for porters:

- A handler that throws lands at the configured
  `internal_error_handler` (a `std::function<http_response(const http_request&, std::string_view)>`).
  The `string_view` carries `what()` from the caught exception. Default
  behaviour: log and return `500`.
- Calling an API entry point whose backend feature is disabled at
  configure time (e.g. `ssl_cert(...)` on a `HAVE_GNUTLS=no` build)
  throws `feature_unavailable` — a public `std::runtime_error` subclass.
  Build-flag-disabled features therefore surface at **runtime** rather
  than disappearing from the API at compile time; query
  `ws.features()` to branch defensively.
- The framework never swallows exceptions silently. Anything that
  escapes a handler is logged via the configured `log_error` callback
  before the `internal_error_handler` fires.

## Build prerequisites

- **C++20 floor.** libhttpserver 1.x's last release was C++17-compatible;
  v2.0 drops C++17. Concepts, `<bit>`, `<span>`, and `consteval` appear
  in public headers.
- **Toolchains known good out of the box:** Debian 13 GCC 14.2,
  RHEL 10 GCC 14, FreeBSD 14 Clang 18+, current Apple Clang, and
  Homebrew GCC 15+.
- **RHEL 9.** Stock GCC 11 on RHEL 9 is too old. Install Red Hat's
  `gcc-toolset-14` overlay and `source scl_source enable gcc-toolset-14`
  before configuring.

See [specs/architecture/08-build-and-packaging.md](specs/architecture/08-build-and-packaging.md)
for the full matrix.

## SOVERSION and packaging

SOVERSION bumps from `1` to `2`. v2 ships as `libhttpserver2` (binary)
with `libhttpserver2-dev` / `libhttpserver2-devel` for headers and
pkg-config. v1's `libhttpserver1` and v2's `libhttpserver2` are
**parallel-installable**: applications mid-port can link against both
side by side, including under the same `/usr` prefix.

There is **no inline namespace** and **no symbol-versioning script**:
v2 is a clean rename, not an ABI overlay over v1. v1.x is end-of-life
on the day v2.0 ships — there is no v1 maintenance branch (PRD §1,
DR-011 ([specs/architecture/11-decisions/DR-011.md](specs/architecture/11-decisions/DR-011.md))).

## See also

- [README.md](README.md) — full v2.0 introduction and worked examples.
- [examples/](examples/) — every example is v2.0-idiomatic.
- [specs/product_specs.md](specs/product_specs.md) §3.1–§3.7 — the
  authoritative requirement set behind each change.
- [ChangeLog](ChangeLog) — formal per-version log.
