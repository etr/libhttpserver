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
  `no_dual_stack`, `no_alpn`, `no_thread_safety`, `no_listen_socket`,
  `no_put_processed_data_to_content`, and
  `no_generate_random_filename_on_upload` are removed (the full v1
  `no_*` family). The remaining single setter takes a `bool`:
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
- **v1 compat auth-handler shim.** `httpserver::compat::auth_handler_v1_ptr`,
  `httpserver::compat::adapt_legacy_auth`, and the
  `[[deprecated]] create_webserver::auth_handler(compat::auth_handler_v1_ptr)`
  overload are removed (TASK-067). The transitional shim that let v1
  callers spell their auth handler as
  `std::function<std::shared_ptr<http_response>(const http_request&)>`
  was scheduled for removal "in the next release"; this is that removal.
  Migrate to the canonical `auth_handler_ptr` returning
  `std::optional<http_response>` (`std::nullopt` to allow, engaged value
  to reject). The `namespace httpserver::compat` is dissolved with no
  surviving members. **Source incompatibility:** TUs that called
  `auth_handler(<legacy shape>)` fail to compile against v2.1 headers;
  the legacy callable is no longer implicitly convertible to
  `auth_handler_ptr`. There is no runtime ABI surface to break — the
  overload was a header-only inline forwarder.
- **`create_webserver::validator(...)` dispatch semantics — confirmed
  never wired (TASK-078).** The `validator` builder method and the
  `validator_ptr` callback typedef on `create_webserver` are retained
  as inert v1 surface — the callback has not been invoked from the
  dispatch path since commit `9163a4f` (Jan 2013, "Eliminated unescaper
  and validator delegates"). v2 ships the surface unchanged for source
  compatibility, but a configured validator callback is dead code. The
  v2 replacement is `webserver::add_hook(hook_phase::request_received,
  ...)` returning `hook_action::respond_with(http_response)` to reject
  a request, or `hook_phase::accept_decision` for a connection-scoped
  veto (see `specs/architecture/04-components/hooks.md`). The legacy
  `validator_builder` integ test in `test/integ/basic.cpp` exercised
  no validation behaviour (it only asserted the server booted with a
  validator set) and has been removed; the equivalent compile-time
  builder pin survives at
  `test/unit/create_webserver_test.cpp::builder_validator`.
- **v1 `registered_resources*` registration maps (internal).** No
  public-API change. Dispatch already routed through the v2 3-tier
  route table (`lookup_v2()`) after TASK-053; the residual
  registration-time maps (`registered_resources`,
  `registered_resources_str`, `registered_resources_regex`) and their
  shared mutex are now deleted (TASK-067). Lambda/class path-conflict
  detection consults the v2 route table directly via
  `find_v2_entry_by_path_`; duplicate-registration detection moved
  inside `register_v2_route`; the WebSocket handler registry gains a
  dedicated `registered_ws_handlers_mutex_`. Callers observe no
  behavioural change.

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
- **Lifecycle hook bus (`webserver::add_hook` / `http_resource::add_hook`).**
  Eleven phases (`hook_phase` enum: `connection_opened`,
  `accept_decision`, `request_received`, `body_chunk`,
  `route_resolved`, `before_handler`, `handler_exception`,
  `after_handler`, `response_sent`, `request_completed`,
  `connection_closed`) spanning connection, request, routing, handler,
  and response. Five of the phases are short-circuit-capable: their
  handlers return `hook_action` (either `hook_action::pass()` to
  continue normal processing or `hook_action::respond(http_response)` to
  short-circuit with a custom response). Multi-subscriber, server-wide
  and per-route. The v1 single-slot setters (`log_access`,
  `not_found_handler`, `method_not_allowed_handler`,
  `internal_error_handler`, `auth_handler`) survive as documented aliases
  for the corresponding `add_hook` calls. Each registration returns a
  move-only `hook_handle` whose destructor erases the entry;
  `hook_handle::detach()` keeps the registration alive for the
  webserver's lifetime. See `specs/architecture/04-components/hooks.md`
  and [`README.md#lifecycle-hooks`](README.md#lifecycle-hooks). Closes:
  #332 (banned-IP log, `examples/banned_ip_log.cpp`), #281 + #69
  (CLF / time-taken access log, `examples/clf_access_log.cpp`), #273
  (early 413, `examples/early_413.cpp`); partially closes #272
  (per-chunk observation; body steal deferred to v2.1).

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
| `auth_handler_ptr` returning `std::shared_ptr<http_response>` | `auth_handler_ptr` returning `std::optional<http_response>` |

> **Note:** `shoutCAST()` is the sole camelCase survivor in the v2 public API.
> The name maps to the SHOUTcast streaming protocol and is intentionally
> preserved unchanged (PRD §3.7).

## What changed semantically

- **Handlers return `http_response` by value.** v1 returned
  `std::unique_ptr<http_response>` (and earlier `std::shared_ptr<http_response>`);
  v2 returns a value. The framework moves it into the dispatch path. No
  heap allocation is required for small responses (small-buffer optimisation
  inside `http_response`).
- **Centralized auth handler returns `std::optional<http_response>`.**
  The `auth_handler` callback signature is now
  `std::function<std::optional<http_response>(const http_request&)>`
  (earlier v2 work-in-progress shipped
  `std::function<std::shared_ptr<http_response>(const http_request&)>`).
  Return `std::nullopt` to allow the request; return an `http_response`
  to reject. Removes one heap allocation per authenticated request.
- **`http_request` getters return `const&` / `string_view`.** v1's
  `get_header(name)` (and `get_arg`, `get_cookie`, `get_footer`) returned
  by value and inserted an empty entry into the request map on miss; v2's
  versions return a reference / `string_view` and never mutate the
  request. Container getters (`get_headers()`, `get_args()`,
  `get_path_pieces()`, `get_files()`) return `const&` to the underlying
  map.
- **`http_response::get_header` / `get_footer` / `get_cookie` are
  const and do not insert on miss.** Mutation uses the
  `with_header` / `with_footer` / `with_cookie` chain, which returns
  `*this` by reference (`&` on lvalue calls, `&&` on rvalue calls) for
  fluent assembly.
- **`webserver(create_webserver const&)` is `explicit`.** Direct-init
  `webserver ws{cw};` rather than copy-init `webserver ws = cw;`.
- **Default internal-error response body is now a fixed string.** v1's
  default 500 body included the originating exception's `e.what()`
  text. v2.0
  ([DR-009 Revision 1](specs/architecture/11-decisions/DR-009.md))
  sends `"Internal Server Error"` instead, eliminating a CWE-209
  information-disclosure surface (exception messages routinely embed
  file paths, SQL fragments, internal identifiers, and
  attacker-influenced input). The `log_error` callback continues to
  receive the verbatim message. To restore the v1 verbose-body
  behaviour for development, set `.expose_exception_messages(true)`
  on the builder. Configured `internal_error_handler` callbacks are
  unaffected — they still receive the message and can build any body
  they want.
- **`http_request::operator<<` redacts credentials by default.**
  v1 (and earlier v2 builds) emitted `pass:"<plaintext>"`,
  `Authorization`/`Proxy-Authorization` header values, and cookie values
  verbatim into diagnostic output, leaking every credential into any
  log aggregation pipeline that captures operator-stream dumps
  (CWE-312, CWE-532, OWASP A09:2021). Digest credentials are protected
  through the same Authorization-header path (there was never a separate
  `digested_pass` emit field). v2.0 replaces those fields with the fixed
  token `<redacted>` in the default stream output. To restore the v1
  verbose form for local development, call `.expose_credentials_in_logs(true)`
  on the `create_webserver` builder — this flag is intended for development
  only and must not be set in production deployments.
- **`http_response::pipe` no longer accepts a `size_hint`.** Earlier v2
  work-in-progress builds exposed `pipe(int fd, std::size_t size_hint = 0)`
  with `size_hint` reserved for future use; the parameter was never
  consulted on the dispatch path. v2.0 ships the minimal `pipe(int fd)`
  signature (TASK-063, PRD §3.5 / `PRD-RSP-REQ-001`). Source callers
  passing a second argument must drop it; no behavioural change. There
  is no honoring path planned — `Content-Length` synthesis would lie
  when the pipe yields a different byte count, and libmicrohttpd's
  `MHD_create_response_from_pipe` takes no size.
- **Structured cookie type (TASK-064).** The string-blob cookie API
  on `http_response` is now `[[deprecated]]` in favour of a typed
  `httpserver::cookie` value (new public header `<httpserver/cookie.hpp>`).
  Construct with fluent setters — `cookie{}.with_name(...).with_value(...)
  .with_domain(...).with_path(...).with_expires(epoch_seconds).with_max_age(s)
  .with_secure(true).with_http_only(true).with_same_site(same_site_mode::strict)`
  — then hand to `http_response::with_cookie(cookie)`. The dispatch path
  emits one RFC 6265 §4.1 well-formed `Set-Cookie` header per entry with
  a fixed attribute order (`name=value; Expires; Max-Age; Domain; Path;
  Secure; HttpOnly; SameSite`); `SameSite=None` auto-coerces `Secure` on
  the wire. The matching request-side accessor is `http_request::
  get_cookies_parsed()`, returning `const std::vector<httpserver::cookie>&`
  backed by a per-request lazy cache. Legacy `with_cookie(std::string,
  std::string)`, `get_cookie(...)`, and `get_cookies()` still compile but
  emit `[[deprecated]]`; they will be removed in v2.1. The new APIs reject
  CR/LF/NUL plus `;` in values (attribute-injection guard, CWE-113); the
  pre-TASK-064 wire footgun of `with_cookie("name", "v; Path=/admin")`
  silently emitting attributes is gone.

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
  behaviour: log and return `500`. When no `internal_error_handler` is
  configured, the default response body is the fixed string
  `"Internal Server Error"` (DR-009 Revision 1, CWE-209 fix); set
  `.expose_exception_messages(true)` on the builder to restore the v1
  message-in-body behaviour for development.
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

## Test infrastructure

- **`LT_SKIP` / `LT_SKIP_IF` macros (TASK-076).** The internal
  `test/littletest.hpp` framework now distinguishes SKIP from PASS.
  Test bodies that depend on a runtime resource (for example
  `gnutls-cli` for the PSK connection tests in
  `test/integ/ws_start_stop.cpp`) now call
  `LT_SKIP_IF(cond, reason)` instead of recording a tautological
  `LT_CHECK_EQ(1, 1)`. A binary whose only outcomes are skips exits
  77 (Automake's SKIP code from `test-driver`); a binary with mixed
  skips and successes exits 0 and the runner summary reports
  `-> N skipped`. The TLS lanes in `ws_start_stop.cpp` now skip — not
  silently pass — when GnuTLS or `gnutls-cli` is absent, so a build
  that silently lost TLS support is no longer hidden behind a green
  CI signal. Two new CI lanes (`tls-no-cli`, plus a baseline canary)
  assert the SKIP wiring fires (or does not) as configured.

- **`<pthread.h>` runtime-sentinel guard removed (TASK-081).** The
  pthread leak detector in `test/unit/header_hygiene_test.cpp` (and
  its companion regex slot in `Makefile.am`'s `HEADER_HYGIENE_FORBIDDEN`)
  was unable to fire on any CI lane libhttpserver actually runs on. The
  libhttpserver public surface uses STL container headers
  (`std::string`, `std::vector`, `std::map`, ...) and both mainstream
  C++ standard libraries — libc++ via
  `<__thread/support/pthread.h>` and libstdc++ in thread-enabled mode
  via `<bits/gthr-default.h>` — unconditionally drag `<pthread.h>` in
  from those headers. Because the public surface cannot be rewritten
  to drop STL containers without a source-incompatible break, the
  pthread guard is structurally unsatisfiable on every supported
  configuration. The detector was deleted rather than kept as
  dead code gated on STL-implementation-detection macros. The
  remaining hygiene sentinels (`<microhttpd.h>`, `<gnutls/gnutls.h>`,
  `<sys/socket.h>`, `<sys/uio.h>`) continue to run on every CI lane.

- **Parallel-install acceptance gate is now live in per-PR CI
  (TASK-089).** The TASK-044 parallel-installability check
  (`scripts/check-parallel-install.sh`, which builds `libhttpserver1`
  from `master` alongside `libhttpserver2` and asserts both SONAMEd
  shared libraries coexist in one DESTDIR) was previously opt-in only
  (`make check-parallel-install`) and degraded to a silent pass (exit
  0) on five environment-quirk paths — so a PR that broke
  parallel-installability never actually failed anything. It now runs
  on a single baseline Linux gcc/libstdc++ lane in
  `.github/workflows/verify-build.yml` (matrix key
  `parallel-install: check`), and the five environment-quirk paths
  (master ref missing, `git worktree add` failed, v1
  bootstrap/configure/make failed) emit a `SKIP` and **fail the job**
  rather than passing. The escape hatch for genuine infrastructure
  breakage is the environment variable
  `HTTPSERVER_ALLOW_PARALLEL_INSTALL_SKIP=1`, which is intentionally
  not set on the CI lane so skips stay fatal. A structural gate
  (`scripts/check-parallel-install-lane.sh`, run on the `lint` lane)
  guards the CI wiring against silent drift, and a unit test
  (`scripts/test_check_parallel_install.sh`, run in every lane via
  `make check`) pins the skip-or-fail exit-code contract.

## See also

- [README.md](README.md) — full v2.0 introduction and worked examples.
- [examples/](examples/) — every example is v2.0-idiomatic.
- [specs/product_specs.md](specs/product_specs.md) §3.1–§3.7 — the
  authoritative requirement set behind each change.
- [specs/architecture/13-documentation.md](specs/architecture/13-documentation.md)
  — architecture deliverable definition that names this file as an explicit
  deliverable; the requirement lineage for this document.
- [ChangeLog](ChangeLog) — formal per-version log.
