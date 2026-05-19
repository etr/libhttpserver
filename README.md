# libhttpserver

![GA: Build Status](https://github.com/etr/libhttpserver/actions/workflows/verify-build.yml/badge.svg)
[![codecov](https://codecov.io/gh/etr/libhttpserver/branch/master/graph/badge.svg)](https://codecov.io/gh/etr/libhttpserver)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1bd1e8c21f66400fb70e5a5ce357b525)](https://www.codacy.com/gh/etr/libhttpserver/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=etr/libhttpserver&amp;utm_campaign=Badge_Grade)

[![ko-fi](https://www.ko-fi.com/img/donate_sm.png)](https://ko-fi.com/F1F5HY8B)

libhttpserver is a C++20 library for building RESTful HTTP servers on top of
[GNU libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/). v2.0 is a
lambda-first redesign: a working server is ten lines, handlers are
`std::function`s, responses are value-typed, and every public method is
thread-safe by contract.

This README introduces the v2.0 API. It is not exhaustive: the headers under
[`src/httpserver/`](src/httpserver/) and the [`examples/`](examples/) tree are
the authoritative reference.

```cpp
// libhttpserver hello-world example — the lambda form (PRD §3.4).
// Compiles in ten lines including main(), with no http_resource subclass
// and no raw-pointer ownership. Production code typically qualifies names
// explicitly; the `using namespace` here is a one-off so this file can
// document the shortest possible end-to-end demo. See shared_state.cpp
// for the class-based pattern that is appropriate when handlers must
// share mutable state.
#include <httpserver.hpp>
using namespace httpserver;  // NOLINT(build/namespaces) - keep the demo at <=10 LOC
int main() {
    webserver ws{create_webserver(8080)};
    ws.on_get("/hello", [](const http_request&) {
        return http_response::string("Hello, World!");
    });
    ws.start(true);
}
```

The block above is reproduced byte-for-byte from
[`examples/hello_world.cpp`](examples/hello_world.cpp); a CI gate
(`scripts/check-readme.sh`) enforces the byte-for-byte equality.

## Table of contents

* [Build / install](#build--install)
* [Hello, world — lambda form](#hello-world--lambda-form)
* [Class-form handlers](#class-form-handlers)
* [Request](#request)
* [Response](#response)
* [Routing](#routing)
* [Threading contract](#threading-contract)
* [Error propagation](#error-propagation)
* [Feature availability](#feature-availability)
* [WebSocket](#websocket)
* [Migrating from v1](#migrating-from-v1)
* [Examples index](#examples-index)
* [Community and license](#community-and-license)

## Build / install

**Compiler floor:** C++20. libhttpserver v2.0 will not build with a C++17
compiler.

Supported toolchains:

| Platform | Toolchain | Notes |
|---|---|---|
| Debian 13 (trixie) | GCC 14.2 | Out-of-the-box |
| RHEL 9 | `gcc-toolset-14` | Stock GCC 11 is too old; install the Red Hat toolset overlay |
| RHEL 10 | GCC 14 | Out-of-the-box |
| FreeBSD 14.x | base Clang 18+ | Out-of-the-box |
| macOS | Homebrew GCC 15+ or current Apple Clang | Out-of-the-box |
| vcpkg / Conan | GCC 13+ / Clang 16+ | Out-of-the-box |

**Runtime dependencies:** [GNU libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)
≥ 1.0.0. Optional: GnuTLS (TLS), libgcrypt (digest auth), libmicrohttpd built
with WebSocket support.

**Build:**

```sh
./bootstrap
./configure
make
sudo make install
```

`make check` runs the test suite (unit + integration). `make examples` builds
every program under [`examples/`](examples/).

**ABI / packaging.** SOVERSION bumps from 1 to 2 in v2.0. Distributions
package `libhttpserver2` parallel-installable with `libhttpserver1`. There is
no inline namespace and no symbol-versioning script: v1.x is end-of-life on
the day v2.0 ships, and code that needs both can link them side by side.

## Hello, world — lambda form

The snippet at the top of this README is the entire program. Walking through
it:

* `webserver ws{create_webserver(8080)};` — `create_webserver` is a fluent
  builder for the server configuration; `webserver` is constructed by direct
  initialization from it. `webserver` is non-copyable and non-movable; pass it
  by reference once it exists.
* `ws.on_get("/hello", [](const http_request&) { ... });` — register a
  GET-only handler. The handler is a `std::function<http_response(const
  http_request&)>`. There is no subclass, no `shared_ptr`, no raw pointer.
* `return http_response::string("Hello, World!");` — `http_response` is a
  value type. Factories on `http_response` build common shapes; the fluent
  `with_*` mutators add headers, footers, cookies, and status. The response
  is returned by value into the dispatcher.
* `ws.start(true);` — `true` means *block this thread until the server is
  stopped*. Pass `false` to start the listener and return immediately; later
  call `stop_and_wait()` from another thread.

See [`examples/hello_world.cpp`](examples/hello_world.cpp) and
[`examples/hello_with_get_arg.cpp`](examples/hello_with_get_arg.cpp) for the
complete sources.

## Class-form handlers

Lambdas suffice when each HTTP method is independent. When several methods on
one path share state — a counter, a cache, a mutex — derive from
[`http_resource`](src/httpserver/http_resource.hpp) and register the subclass
once:

```cpp
class counter : public httpserver::http_resource {
 public:
    httpserver::http_response render_get(const httpserver::http_request&) override {
        std::lock_guard lock{m_};
        return httpserver::http_response::string(std::to_string(n_));
    }
    httpserver::http_response render_post(const httpserver::http_request&) override {
        std::lock_guard lock{m_};
        ++n_;
        return httpserver::http_response::string(std::to_string(n_));
    }
 private:
    std::mutex m_;
    int n_ = 0;
};

// ...
ws.register_path("/count", std::make_unique<counter>());
```

The virtual hooks are `render_get`, `render_post`, `render_put`,
`render_delete`, `render_head`, `render_options`, `render_patch`,
`render_connect`, and `render_trace` — all lowercase, all returning
`http_response` by value. The `webserver` takes ownership of the resource
via `std::unique_ptr`.

See [`examples/shared_state.cpp`](examples/shared_state.cpp) for the
canonical example.

## Request

`http_request` is read-only inside a handler. The accessors are designed
around `std::string_view` so reading headers and arguments does not allocate:

| Accessor | Returns | Notes |
|---|---|---|
| `get_path()` | `std::string_view` | The decoded path |
| `get_method()` | `httpserver::http_method` | Strongly-typed enum; see [`http_method.hpp`](src/httpserver/http_method.hpp) |
| `get_arg(name)` | `std::string_view` | First value for a query/form arg |
| `get_arg_flat(name)` | `std::string_view` | Alias for `get_arg`; explicit "first value only" form |
| `get_querystring()` | `std::string_view` | Raw query string (no decoding) |
| `get_version()` | `std::string_view` | "HTTP/1.1", "HTTP/2", … |
| `get_headers()` | `const map&` | All headers |
| `get_args()` | `const map&` | All query/form arguments |
| `get_path_pieces()` | `const vector&` | Split path components |
| `get_files()` | `const map&` | Uploaded files (multipart) |
| `get_user()` | `std::string_view` | Basic-auth user; empty when `HAVE_BAUTH` is off |
| `get_pass()` | `std::string_view` | Basic-auth password; empty when `HAVE_BAUTH` is off |
| `get_digested_user()` | `std::string_view` | Digest-auth user; empty when `HAVE_DAUTH` is off |
| `get_client_cert_*()` | various | TLS-only; return empty / `-1` / `false` when `HAVE_GNUTLS` is off |

**Lifetime contract.** Every `string_view` returned by `http_request` is
valid for the duration of the handler invocation and no longer. Copy what
you need to keep (e.g., into a `std::string`); do not hand a view to a
deferred callback. The references returned by `get_headers()`, `get_args()`,
`get_path_pieces()`, and `get_files()` follow the same rule.

**Method enum.** `http_method` (declared in
[`http_method.hpp`](src/httpserver/http_method.hpp)) covers the canonical
HTTP methods. `method_set` is a bitset used by atomic multi-method
registration (see [Routing](#routing)).

## Response

`http_response` is a value type — move-only, returned by value, never
`shared_ptr`-wrapped. There is no class hierarchy of body subclasses; the
body shape is a runtime detail of one type.

**Factories:**

| Factory | Body shape |
|---|---|
| `http_response::string(body)` | An in-memory string (small bodies live inline via SBO) |
| `http_response::file(path)` | Stream a file from disk |
| `http_response::iovec(entries)` | Scatter-gather over a vector of `iovec_entry` (zero-copy) |
| `http_response::pipe(fd)` | Stream from a pipe / FIFO |
| `http_response::empty()` | Empty body |
| `http_response::deferred(producer)` | Body produced incrementally by a callback |
| `http_response::unauthorized(realm)` | 401 with the proper `WWW-Authenticate` header |

**Fluent mutation.** Every `http_response` exposes `with_status`,
`with_header`, `with_footer`, and `with_cookie` returning `*this` by value
so calls can chain:

```cpp
return httpserver::http_response::string("hi")
    .with_header("X-Trace-Id", trace_id)
    .with_status(201);
```

**Building error responses by value.** There is no throw-as-status idiom.
To return a 404 from a handler, build it explicitly:

```cpp
if (!found) {
    return httpserver::http_response::empty().with_status(404);
}
```

See [`examples/setting_headers.cpp`](examples/setting_headers.cpp),
[`examples/iovec_response_example.cpp`](examples/iovec_response_example.cpp),
[`examples/minimal_file_response.cpp`](examples/minimal_file_response.cpp),
and
[`examples/pipe_response_example.cpp`](examples/pipe_response_example.cpp)
for working programs.

## Routing

The `webserver` exposes three families of registration entry points:

**Per-method, exact path.** `on_get`, `on_post`, `on_put`, `on_delete`,
`on_head`, `on_options`, `on_patch`, `on_connect`, `on_trace` each take a
path and a lambda. Re-registering the same `(method, path)` pair throws.

**Atomic multi-method.** `route(http_method::GET | http_method::HEAD,
"/info", handler)` registers the handler under several methods in a single
critical section; either every slot is registered, or none of them are.
`route(http_method::GET, "/info", handler)` is the single-method form and
overlaps with `on_get`. A `method_set` is the bitwise-or of `http_method`
values.

**Resource registration.** `register_path("/foo", std::make_unique<R>())`
registers an `http_resource` subclass at an exact path;
`register_prefix("/foo/", std::make_unique<R>())` registers it for the
subtree starting at `/foo/`. Parameterized paths use brace syntax:
`register_path("/users/{id}", ...)`; an optional per-segment regex
constrains a parameter: `register_path("/users/{id|[0-9]+}", ...)`.
Unregister with `unregister_path` and `unregister_prefix`.

The lambda overloads (`on_get`, `route`) are sugar on top of the same
internal dispatcher used by `http_resource`. They are interoperable:
within one server, some paths can be lambdas and others can be
`http_resource` subclasses.

See [`examples/url_registration.cpp`](examples/url_registration.cpp) and
[`examples/handlers.cpp`](examples/handlers.cpp).

## Threading contract

Distilled from `specs/architecture/05-cross-cutting.md` §5.1 and DR-008
(`specs/architecture/11-decisions/DR-008.md`):

1. **Public methods on `webserver` are thread-safe and re-entrant from
   inside a handler.** Two exceptions: `stop_and_wait()` and `~webserver()`
   **deadlock** if invoked from a handler thread, because they wait for
   that very thread to drain. Stop the server from a different thread, or
   signal an external stop loop. (`stop_and_wait` is the v2 spelling for
   the v1 "kill" routine.)
2. **Handlers run concurrently on libmicrohttpd worker threads.** The same
   lambda or `http_resource` instance is invoked from many threads at once.
   Any state you share — counters, caches, file handles — must be
   synchronized on your side. The library does not synchronize user state
   for you.
3. **`http_request` is single-threaded per request.** Sharing one
   `http_request` across threads is undefined; the per-request arena makes
   no guarantees outside the calling thread.
4. **`http_response` is value-typed with exclusive ownership.** Returning
   it transfers ownership into the dispatcher. There is no shared mutable
   response object.

## Error propagation

Distilled from `specs/architecture/05-cross-cutting.md` §5.2 and DR-009
(`specs/architecture/11-decisions/DR-009.md`):

1. **A handler that throws `std::exception` is caught.** The library logs
   the exception via the configured `error_logger` and invokes
   `internal_error_handler(request, e.what())`. The handler's return value
   is sent to the client (default: HTTP 500).
2. **A handler that throws something other than `std::exception`** is also
   caught, with `"unknown exception"` substituted for the message.
3. **Library-internal failures during dispatch** (allocation, body
   materialization) flow through the same `internal_error_handler` path.
4. **If `internal_error_handler` itself throws**, the library logs and
   sends a hardcoded 500 with an empty body. There is no third level of
   fallback.
5. **`feature_unavailable` is a normal `std::runtime_error`** — no special
   status mapping. Catch it explicitly if you want to map it to a 503 or
   similar; the library does not.
6. **There is no throw-as-status idiom.** A handler that wants to return
   404, 400, etc. builds the response by value (see [Response](#response)):
   `return httpserver::http_response::empty().with_status(404);`.

Install custom error handlers on the builder:

```cpp
auto cfg = httpserver::create_webserver(8080)
    .not_found_handler([](const httpserver::http_request&) {
        return httpserver::http_response::string("nope").with_status(404);
    })
    .method_not_allowed_handler([](const httpserver::http_request&) {
        return httpserver::http_response::empty().with_status(405);
    })
    .internal_error_handler([](const httpserver::http_request&, std::string_view what) {
        return httpserver::http_response::string(std::string{what}).with_status(500);
    });
httpserver::webserver ws{cfg};
```

## Feature availability

Several capabilities are gated by build-time flags. v2.0 makes the gating
visible at the API level so application code does not need preprocessor
guards on `HAVE_*` macros.

| Build flag | When disabled | Public-API behavior |
|---|---|---|
| `HAVE_BAUTH` | Basic-auth disabled | `get_user`, `get_pass` return empty `string_view`; `features().basic_auth == false`; `create_webserver::basic_auth(true)` throws `feature_unavailable` at `webserver` construction |
| `HAVE_DAUTH` | Digest-auth disabled | `get_digested_user` returns empty; `check_digest_auth` returns a sentinel result; `features().digest_auth == false` |
| `HAVE_GNUTLS` | TLS disabled | All `get_client_cert_*` accessors return empty / `-1` / `false`; `features().tls == false`; `create_webserver::use_ssl(true)` throws `feature_unavailable` |
| `HAVE_WEBSOCKET` | WebSocket disabled | `register_ws_resource` throws `feature_unavailable`; `features().websocket == false` |

**Probing at runtime.** `webserver::features()` returns a small struct of
four `bool`s — one per flag — so callers can branch without preprocessor
help:

```cpp
if (ws.features().tls) {
    // safe to call get_client_cert_*
}
```

**`feature_unavailable`.** Derives from `std::runtime_error`. Its
`what()` names both the disabled feature and the build flag that gates
it, so log lines pinpoint which flag a deployment is missing. Catch it
where you call a feature-gated method:

```cpp
try {
    ws.register_ws_resource("/sock", std::make_unique<my_socket>());
} catch (const httpserver::feature_unavailable& e) {
    std::cerr << "websocket support is not available: " << e.what() << '\n';
}
```

Block lists, IP-allow handling, and similar features that do not depend on
external libraries are always available: `webserver::block_ip(addr)` and
`webserver::unblock_ip(addr)` install and clear per-server blocks at
runtime.

## WebSocket

WebSocket handlers are registered with `register_ws_resource`, taking
ownership of a `websocket_handler` subclass:

```cpp
ws.register_ws_resource("/echo", std::make_unique<echo_handler>());
// or
auto handler = std::make_shared<echo_handler>();
ws.register_ws_resource("/echo", handler);
```

On a build with `HAVE_WEBSOCKET` disabled — for example, when the system
libmicrohttpd was built without WebSocket support — `register_ws_resource`
throws `feature_unavailable`. See
[`examples/websocket_echo.cpp`](examples/websocket_echo.cpp).

## Migrating from v1

v2.0 is a single breaking release. There is no opt-in compatibility shim:
every v1 client has at least one shape change to make (response factories,
handler signatures, threading contract, error propagation). The v1.x line
is end-of-life on the day v2.0 ships, and v2 is packaged as
`libhttpserver2` — parallel-installable with `libhttpserver1`, so old
binaries keep running while you port.

The rename/removed/added cheat sheet — every API surface that moved — lives
in [`RELEASE_NOTES.md`](RELEASE_NOTES.md).

## Examples index

Every program under [`examples/`](examples/) is a standalone `.cpp` that
links against libhttpserver. The grouped index (HTTP basics, response
shapes, request features, authentication, TLS, WebSocket, performance,
diagnostics) lives in [`examples/README.md`](examples/README.md).

Start with:

* [`examples/hello_world.cpp`](examples/hello_world.cpp) — the ten-line
  lambda form quoted at the top of this README.
* [`examples/shared_state.cpp`](examples/shared_state.cpp) — when the
  class form is the right shape.
* [`examples/setting_headers.cpp`](examples/setting_headers.cpp) — fluent
  `with_header` chaining.
* [`examples/url_registration.cpp`](examples/url_registration.cpp) —
  paths, prefixes, parameters, and per-segment regex constraints.
* [`examples/custom_error.cpp`](examples/custom_error.cpp) — installing
  `not_found_handler`, `method_not_allowed_handler`, and
  `internal_error_handler`.

## Community and license

* [Code of Conduct](CODE_OF_CONDUCT.md)
* [Contributing](CONTRIBUTING.md)
* The library is distributed under the GNU LGPL — see [`COPYING.LESSER`](COPYING.LESSER).
* This documentation is distributed under the GNU FDL — see [`LICENSE`](LICENSE).
