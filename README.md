<!---
Copyright (C)  2011-2026  Sebastiano Merlino.
    Permission is granted to copy, distribute and/or modify this document
    under the terms of the GNU Free Documentation License, Version 1.3
    or any later version published by the Free Software Foundation;
    with no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.
    A copy of the license is included in the LICENSE file at the repository
    root, and online at <https://www.gnu.org/licenses/fdl-1.3.html>.
-->

# The libhttpserver reference manual

![GA: Build Status](https://github.com/etr/libhttpserver/actions/workflows/verify-build.yml/badge.svg)
[![codecov](https://codecov.io/gh/etr/libhttpserver/branch/master/graph/badge.svg)](https://codecov.io/gh/etr/libhttpserver)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1bd1e8c21f66400fb70e5a5ce357b525)](https://www.codacy.com/gh/etr/libhttpserver/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=etr/libhttpserver&amp;utm_campaign=Badge_Grade)

[![ko-fi](https://www.ko-fi.com/img/donate_sm.png)](https://ko-fi.com/F1F5HY8B)

## Tl;dr

libhttpserver is a C++20 library for building high-performance RESTful HTTP
servers on top of [GNU libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/).
v2.0 is a lambda-first redesign: a working server is ten lines, handlers are
`std::function`s, responses are value-typed, and every public method is
thread-safe by contract.

**Features:**

- HTTP/1.1-compatible request parser
- RESTful-oriented interface
- Lambda-first handler API; class-form handlers (`http_resource`) when state is shared
- Value-typed responses with seven factories and fluent `with_*` chaining
- Cross-platform (Linux, *BSD, macOS, Windows / MSYS2-MinGW)
- Multiple threading models (internal poll, internal select, thread pool, external loop)
- IPv6 and dual-stack support
- Optional TLS (HTTPS) via GnuTLS, including mTLS, SNI, and TLS-PSK
- Optional Basic and Digest authentication (SHA-256, SHA-512/256, MD5)
- Optional centralized authentication with path-based skip rules
- Optional WebSocket support (requires libmicrohttpd built with WebSocket support)
- Response shapes for every common body: string, file, iovec scatter-gather, pipe streaming, deferred callback, empty, and 401-unauthorized
- Incremental POST processing (form-urlencoded, multipart/form-data, file uploads)
- External event-loop integration (`run`, `run_wait`, `get_fdset`, `add_connection`)
- Daemon introspection (bound port, active connections, listen FD)
- Turbo mode, TCP_FASTOPEN, suppressed `Date` headers, configurable backlog
- IP access control with wildcard ranges — deny list (`deny_ip`) and allow list (`allow_ip`), IPv4 & IPv6
- Architecture contracts you can rely on: threading (DR-008 / §5.1) and error propagation (DR-009 / §5.2)

This README is a guided reference: it walks the v2.0 API surface section by
section. It is comprehensive but not exhaustive — the headers under
[`src/httpserver/`](src/httpserver/) are the source of truth, and the
[`examples/`](examples/) tree contains a runnable demonstration of every
feature.

The shortest possible server looks like this:

```cpp
// Copyright 2026 Sebastiano Merlino
// libhttpserver hello-world example — the lambda form.
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

* [Introduction](#introduction)
* [Requirements](#requirements)
* [Building and installation](#building-and-installation)
* [Getting started — Hello, world](#getting-started--hello-world)
* [Class-form handlers](#class-form-handlers)
* [Structures and classes type definition](#structures-and-classes-type-definition)
* [Create and work with a webserver](#create-and-work-with-a-webserver)
* [The resource object](#the-resource-object)
* [Routing](#routing)
* [Request](#request)
* [Response](#response)
* [IP blocking and unblocking](#ip-blocking-and-unblocking)
* [Authentication](#authentication)
* [WebSocket support](#websocket-support)
* [Daemon introspection and external event loops](#daemon-introspection-and-external-event-loops)
* [HTTP utils](#http-utils)
* [Lifecycle hooks](#lifecycle-hooks)
* [Threading contract](#threading-contract)
* [Error propagation](#error-propagation)
* [Feature availability](#feature-availability)
* [Migrating from v1](#migrating-from-v1)
* [Examples](#examples)

#### Community

* [Code of Conduct](CODE_OF_CONDUCT.md)
* [Contributing](CONTRIBUTING.md)

#### Appendices

* [Copying](#copying) — short statement of the documentation license.
* [GNU-LGPL](COPYING.LESSER) — the GNU Lesser General Public License (the library's code license; full text in `COPYING.LESSER`).
* [GNU-FDL](LICENSE) — the GNU Free Documentation License (this manual's license; full text in `LICENSE`).
* [Thanks](#thanks)

## Introduction

libhttpserver is meant to constitute an easy system to build HTTP servers in
the REST fashion. It is built on top of [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/)
and, like its substrate, it is a daemon library. The mission is to expose
every useful HTTP capability through a simple, modern C++ API so that
application code can focus on business logic rather than on the mechanics of
HTTP request handling.

**Design.** libhttpserver is lambda-first. The shortest server is ten
lines and uses no inheritance; handlers are
`std::function<http_response(const http_request&)>`; responses are
value-typed with seven factories and fluent `with_*` chaining; ownership
of resource-form handlers is expressed in `std::unique_ptr` and
`std::shared_ptr`. The class form (`http_resource`) is the right shape
when several methods on one path share mutable state.

**Two contracts are load-bearing** — and both are stated explicitly in this
README rather than left implicit:

- **Thread-safety** (DR-008 / §5.1). Every public method on `webserver` is
  thread-safe and re-entrant from inside a handler, with two
  documented exceptions (`stop()`, `~webserver()`) that deadlock if invoked
  from a handler thread.
- **Error propagation** (DR-009 / §5.2). A handler that throws is caught
  by the dispatcher and routed through `internal_error_handler`. There is
  no throw-as-status idiom — to send a 404 from a handler, build it by value
  with `http_response::empty().with_status(404)`.

libhttpserver decodes form bodies automatically: `application/x-www-form-urlencoded`
and `multipart/form-data` are parsed into the request's argument map. Files
attached to multipart uploads are exposed through `http_request::get_files()`.

All functions are reentrant and thread-safe unless explicitly stated otherwise.
Clients can also specify resource limits on overall connection count, per-IP
connection count, and per-connection memory to avoid resource exhaustion.

[Back to TOC](#table-of-contents)

## Requirements

libhttpserver can be used without any dependencies aside from libmicrohttpd.

The minimum versions required are:

* g++ >= 11 or clang >= 13 (Apple Clang from Xcode 15+)
* C++20 or newer
* libmicrohttpd >= 1.0.0
* *[Optionally]* for TLS (HTTPS) support, you'll need [libgnutls](http://www.gnutls.org/).
* *[Optionally]* to compile the code reference, you'll need [doxygen](http://www.doxygen.nl/).

On **RHEL 9** (and derivatives), the stock GCC 11 is too old for some C++20
library features the build relies on; install the `gcc-toolset-14` package
and `source /opt/rh/gcc-toolset-14/enable` before configuring.

Additionally, for MinGW on Windows you will need:

* libwinpthread (for MinGW-w64, if you use thread model `posix` you already have this)

For versions before 0.18.0, on MinGW you will need:

* libgnurx >= 2.5.1

The test cases use [libcurl](http://curl.haxx.se/libcurl/) but you don't need
it to compile the library itself.

Please refer to the readme file for your particular distribution if there is
one for important notes.

| Platform | Toolchain | Notes |
|---|---|---|
| Debian 13 (trixie) | GCC 14.2 | Out-of-the-box |
| RHEL 9 | `gcc-toolset-14` | Stock GCC 11 is too old; install the Red Hat toolset overlay |
| RHEL 10 | GCC 14 | Out-of-the-box |
| FreeBSD 14.x | base Clang 18+ | Out-of-the-box |
| macOS | Homebrew GCC 15+ or current Apple Clang | Out-of-the-box |
| vcpkg / Conan | GCC 13+ / Clang 16+ | Out-of-the-box |

[Back to TOC](#table-of-contents)

## Building and installation

libhttpserver uses the standard autotools workflow. The usual build process is:

```sh
./bootstrap
mkdir build
cd build
../configure
make
sudo make install
```

`make check` runs the test suite (unit + integration). `make examples`
builds every program under [`examples/`](examples/). `make dist` produces
a portable source tarball.

**ABI / packaging.** SOVERSION is 2. Distributions package
`libhttpserver2`, parallel-installable with prior major versions so
applications can link the major they need side by side. There is no
inline namespace and no symbol-versioning script.

[Back to TOC](#table-of-contents)

### Optional parameters to the configure script

A complete list of parameters can be obtained by running `./configure --help`.
The libhttpserver-specific options are listed below (the canonical configure
options are also supported):

* `--enable-same-directory-build` — enable in-tree builds. Strongly discouraged. *(default: no)*
* `--enable-debug` — enable debug data generation. *(default: no)*
* `--disable-doxygen-doc` — don't generate any Doxygen documentation. Doxygen is automatically invoked when present on the system and disabled otherwise.
* `--enable-fastopen` — enable use of TCP_FASTOPEN. *(default: yes)*
* `--enable-static` — enable static linking. *(default: yes)*

Build-time feature flags (auto-detected, can be forced):

* `--with-gnutls` / `--without-gnutls` — enable/disable TLS support. Sets the `HAVE_GNUTLS` macro.
* `--with-basic-auth` / `--without-basic-auth` — enable/disable Basic auth. Sets `HAVE_BAUTH`.
* `--with-digest-auth` / `--without-digest-auth` — enable/disable Digest auth. Sets `HAVE_DAUTH`.
* `--with-websocket` / `--without-websocket` — enable/disable WebSocket support. Sets `HAVE_WEBSOCKET`. Requires libmicrohttpd built with WebSocket support.

See [Feature availability](#feature-availability) for how each of these flags
affects the runtime API.

[Back to TOC](#table-of-contents)

### Building on Windows (MSYS2)

MSYS2 provides multiple shell environments with different purposes.
Understanding which shell to use is important:

| Shell | Host triplet | Runtime dependency | Use case |
|-------|--------------|--------------------|----------|
| **MinGW64** | `x86_64-w64-mingw32` | Native Windows | **Recommended** for native Windows apps |
| **MSYS** | `x86_64-pc-msys` | `msys-2.0.dll` | POSIX-style apps, build tools |

**Recommended: use the MinGW64 shell** for building libhttpserver to
produce native Windows binaries without additional runtime dependencies.

#### Step-by-step build instructions

1. Install [MSYS2](https://www.msys2.org/).
2. Open the **MINGW64** shell (not the MSYS shell) from the Start Menu.
3. Install dependencies:

```sh
pacman -S --needed mingw-w64-x86_64-{gcc,libtool,make,pkg-config,doxygen,gnutls,curl} autotools
```

4. Build and install [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) (>= 1.0.0).
5. Build libhttpserver:

```sh
./bootstrap
mkdir build && cd build
../configure --disable-fastopen
make
make check  # run tests
```

**Important:** the `--disable-fastopen` flag is required on Windows as
TCP_FASTOPEN is not supported there.

#### If you use the MSYS shell

Building from the MSYS shell also works, but the resulting binaries will
depend on `msys-2.0.dll`. The configure script will emit a warning when
building in this environment:

```
configure: WARNING: Building from MSYS environment. Binaries will depend on msys-2.0.dll.
```

Consider switching to the MinGW64 shell for native Windows binaries.

#### Library files on Windows

When building with GCC-based toolchains (MSYS2/MinGW, Cygwin), the
following library files are generated:

| File | Purpose |
|------|---------|
| `libhttpserver.a` | Static library archive |
| `libhttpserver.dll` | Shared library (DLL) |
| `libhttpserver.dll.a` | Import library for linking against the DLL |
| `libhttpserver.la` | Libtool archive (used by libtool during linking) |

**Note about `.lib` files:** the `.dll.a` format is the import library format
used by GCC toolchains. If you're looking for `.lib` files, those are the
MSVC (Microsoft Visual C++) import library format and are only generated
when building with the MSVC toolchain. The `.dll.a` file serves the same
purpose as `.lib` but for GCC-based compilers.

**Linking against libhttpserver:**

Using pkg-config (recommended):

```sh
g++ myapp.cpp $(pkg-config --cflags --libs libhttpserver) -o myapp
```

Manual linking:

```sh
g++ myapp.cpp -I/mingw64/include -L/mingw64/lib -lhttpserver -lmicrohttpd -o myapp
```

[Back to TOC](#table-of-contents)

## Getting started — Hello, world

The Tl;dr at the top of this README contains the entire program. Walking
through it:

* `webserver ws{create_webserver(8080)};` — `create_webserver` is a fluent
  builder for the server configuration; `webserver` is constructed by
  direct initialisation from the builder. `webserver` is non-copyable and
  non-movable; pass it by reference once it exists. The constructor is
  `explicit`, so callers must direct-init rather than rely on implicit
  conversion.
* `ws.on_get("/hello", [](const http_request&) { ... });` — register a
  GET-only handler. The handler is a
  `std::function<http_response(const http_request&)>`. There is no
  subclass, no `shared_ptr`, no raw pointer. The seven HTTP-verb
  shortcuts are `on_get`, `on_post`, `on_put`, `on_delete`, `on_patch`,
  `on_options`, and `on_head`. For CONNECT and TRACE, use the
  `route(http_method::CONNECT, ...)` and `route(http_method::TRACE, ...)`
  forms covered under [Routing](#routing).
* `return http_response::string("Hello, World!");` — `http_response` is
  a value type. Factories on `http_response` build common shapes; the
  fluent `with_*` mutators add headers, footers, cookies, and status.
  The response is returned by value into the dispatcher.
* `ws.start(true);` — `true` means *block this thread until the server
  is stopped*. Pass `false` to start the listener and return immediately;
  later call `stop_and_wait()` from another thread.

To test the example, you can run the following from a terminal:

```sh
curl -XGET -v http://localhost:8080/hello
```

See [`examples/hello_world.cpp`](examples/hello_world.cpp) and
[`examples/hello_with_get_arg.cpp`](examples/hello_with_get_arg.cpp) for
the complete sources.

[Back to TOC](#table-of-contents)

## Class-form handlers

Lambdas suffice when each HTTP method is independent. When several methods
on one path share state — a counter, a cache, a mutex — derive from
[`http_resource`](src/httpserver/http_resource.hpp) and register the
subclass once:

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
`http_response` by value. The `webserver` takes ownership of the
resource via `std::unique_ptr` (or a `std::shared_ptr` overload — see
[Routing](#routing)).

See [`examples/shared_state.cpp`](examples/shared_state.cpp) for the
canonical example.

[Back to TOC](#table-of-contents)

## Structures and classes type definition

This is the cast of types you will work with. Each is detailed further on
in its own section.

* **`webserver`** — represents the daemon listening on a socket for HTTP
  traffic. Non-copyable, non-movable. See [Create and work with a
  webserver](#create-and-work-with-a-webserver).
  * **`create_webserver`** — fluent builder for the configuration that
    `webserver`'s `explicit` constructor consumes. See the same section.
* **`http_resource`** — base class for a logical collection of HTTP
  methods bound to a path. Override the snake_case `render_*` virtuals.
  See [The resource object](#the-resource-object).
* **`http_request`** — the request passed to handlers. Read-only,
  single-threaded per request. See [Request](#request).
* **`http_response`** — the value-typed response. Seven factories cover
  every body shape; fluent `with_*` mutators add headers, footers,
  cookies, and status. See [Response](#response).
  * Factories: `http_response::string`, `http_response::file`,
    `http_response::iovec`, `http_response::pipe`, `http_response::empty`,
    `http_response::deferred`, `http_response::unauthorized`.
* **`http_method`** — strongly-typed enum of HTTP verbs (GET, POST, PUT,
  DELETE, HEAD, OPTIONS, PATCH, CONNECT, TRACE). Declared in
  [`http_method.hpp`](src/httpserver/http_method.hpp).
* **`method_set`** — small bitset of `http_method` values, used by
  `route()` for atomic multi-method registration.
* **`iovec_entry`** — element type for `http_response::iovec` scatter-gather
  responses. A `{ void* base, size_t length }` pair where the base
  pointer is borrowed for the lifetime of the response.
* **`websocket_handler`** — base class for handling WebSocket
  connections; derive and implement `on_message`. See [WebSocket
  support](#websocket-support).
  * **`websocket_session`** — represents an active WebSocket connection;
    methods to send text, binary, ping/pong, and close frames.
* **`feature_unavailable`** — `std::runtime_error` subclass thrown when
  application code requests a feature that was compiled out (no TLS,
  no Basic auth, no Digest auth, no WebSocket). See [Feature
  availability](#feature-availability).

[Back to TOC](#table-of-contents)

## Create and work with a webserver

Creating a webserver with a standard configuration is the one-liner shown
in the Tl;dr:

```cpp
webserver ws{create_webserver(8080)};
```

`create_webserver` is a *fluent builder*: every setter returns `*this` so
calls chain. The `webserver` constructor is **`explicit`**: callers must
direct-initialise (`webserver ws{cw};`) rather than rely on implicit
conversion. The builder validates eagerly where the input domain
is well-defined — `port` rejects values outside `[0, 65535]`, every `int`
setter rejects negatives, etc. — and raises `std::invalid_argument` at the
setter. Feature-gated settings (`use_ssl`, `basic_auth`, `digest_auth`,
WebSocket registration) are validated by the `webserver` constructor, not
the setter; so a builder configured for an unsupported feature throws
`feature_unavailable` from `webserver(create_webserver)` rather than from
the chained call.

A complete chained example:

```cpp
webserver ws{create_webserver(8080)
    .max_threads(4)
    .max_connections(1024)
    .connection_timeout(30)
    .start_method(http::http_utils::INTERNAL_SELECT)
    .turbo()
    .suppress_date_header()};
```

The rest of this section is a reference of every group of options.

### Basic startup options

* **`.port(uint16_t p)` / `.port(int p)`** — the port the server listens
  on. Can also be passed to the `create_webserver` constructor:
  `create_webserver(8080)`. The `int` overload exists so that
  out-of-`uint16_t` values like `70000` are expressible and rejected with
  `std::invalid_argument`; the `uint16_t` overload keeps type-safe call
  sites clean.
* **`.max_connections(int n)`** — maximum number of concurrent connections
  to accept. The default is `FD_SETSIZE − 4` (the maximum number of file
  descriptors supported by `select` minus four for `stdin`, `stdout`,
  `stderr`, and the server socket). That is, the default is as large as
  possible. Note that if you set a low limit, browsers doing request
  pipelining can saturate it.
* **`.max_threads(int n)`** — size of the internal worker pool, or 0 to
  spawn a thread per connection. Default 0.
* **`.memory_limit(int bytes)`** — per-connection memory limit. Default 0
  (libmicrohttpd default ≈ 32 kB).
* **`.content_size_limit(size_t bytes)`** — cap on individual request
  body size. Default `SIZE_MAX` (unlimited).
* **`.connection_timeout(int seconds)`** — idle timeout for a
  connection. Default 180.
* **`.per_IP_connection_limit(int n)`** — cap on concurrent connections
  from any single client IP. Default 0 (no limit).
* **`.max_thread_stack_size(int bytes)`** — stack size for internal
  worker threads. Default 0 (libmicrohttpd default).

### Listener, socket, and threading options

* **`.start_method(http::http_utils::start_method_T m)`** — pick the
  libmicrohttpd polling mode. Values include `INTERNAL_SELECT` (default),
  `INTERNAL_POLL`, `INTERNAL_EPOLL`, and `EXTERNAL_SELECT` (for use with
  `run` / `run_wait`).
* **`.bind_address(const sockaddr* a)` / `.bind_address(const std::string& ip)`** — bind
  to a specific address. The string overload accepts either IPv4
  (`"127.0.0.1"`) or IPv6 (`"::1"`).
* **`.bind_socket(int fd)`** — use a pre-existing listening socket.
  Useful for socket-activation setups (systemd, launchd).
* **`.use_ipv6(bool = true)`** — bind to an IPv6 socket. (Positive-polarity
  form; see [`RELEASE_NOTES.md`](RELEASE_NOTES.md) for the rename.)
* **`.use_dual_stack(bool = true)`** — accept both IPv4 and IPv6 on the
  same socket.
* **`.listen_socket(bool = true)`** — own the listening socket. Pass
  `false` when feeding connections in via `add_connection()`.
* **`.thread_safety(bool = true)`** — internal locking. Defaults to
  `true`; disable only if you understand the consequences.
* **`.alpn(bool = true)`** — enable ALPN negotiation for HTTPS.
* **`.listen_backlog(int n)`** — `listen(2)` backlog. Default 0
  (libmicrohttpd default).
* **`.address_reuse(int v)`** — `SO_REUSEADDR` / `SO_REUSEPORT` mode.
* **`.tcp_nodelay(bool = true)`** — disable Nagle on accepted sockets.
* **`.tcp_fastopen_queue_size(int n)`** — TCP_FASTOPEN queue size.
  Requires `--enable-fastopen` at configure time.
* **`.connection_memory_increment(size_t v)`** — incremental memory
  allocation per connection.
* **`.sigpipe_handled_by_app(bool = true)`** — tell libmicrohttpd that
  the application installs its own `SIGPIPE` handler.
* **`.client_discipline_level(int v)`** — strictness level for malformed
  request handling. `-1` means default.
* **`.turbo(bool = true)`** — enable libmicrohttpd's `USE_TURBO` flag
  (sacrifices some safety checks for performance).
* **`.suppress_date_header(bool = true)`** — omit the `Date:` response
  header (small perf win at the cost of one piece of HTTP metadata).

### Logging and validation

* **`.log_access(std::function<void(const std::string&)> cb)`** — invoked
  once per accepted request with a single-line summary suitable for an
  access log. Runs from MHD worker threads; **must be thread-safe**.
* **`.log_error(std::function<void(const std::string&)> cb)`** — invoked
  for errors the library decides are worth reporting. Same thread-safety
  contract.
* **`.validator(std::function<bool(const std::string&)> v)`** — called
  early on every request; returning `false` rejects the request.
* **`.unescaper(std::function<...> u)`** — overrides the URL-unescape
  step. Default unescaper handles the canonical HTTP escapes.

### Feature toggles (positive polarity)

* **`.basic_auth(bool = true)`** — enable Basic auth handling.
* **`.digest_auth(bool = true)`** — enable Digest auth handling.
* **`.use_ssl(bool = true)`** — enable TLS.
* **`.deferred(bool = true)`** — enable libmicrohttpd's deferred-response
  internal optimisations. Required to use `http_response::deferred(...)`.
* **`.debug(bool = true)`** — verbose libmicrohttpd debug output.
* **`.pedantic(bool = true)`** — strict HTTP-RFC parsing.
* **`.regex_checking(bool = true)`** — validate path regexes at
  registration. Default `true`.
* **`.ip_access_control(bool = true)`** — enable the IP access-control
  machinery used by `deny_ip` / `allow_ip` (and their `remove_*`
  counterparts). Default `true`. When `false`, every connection is
  admitted and both lists are ignored.
* **`.post_process(bool = true)`** — auto-parse `application/x-www-form-urlencoded`
  and `multipart/form-data` bodies into the request's argument map.
  Default `true`.
* **`.put_processed_data_to_content(bool = true)`** — also expose
  processed POST data through the raw content accessor.
* **`.single_resource(bool = true)`** — short-circuit the resource lookup
  when only one resource is registered.

> The feature toggles all use positive polarity: pass `false` to disable
> (`basic_auth(false)`, `use_ssl(false)`, etc.).

### TLS (HTTPS) options

These are no-ops unless the build has GnuTLS support. On a GnuTLS-off
build, `webserver(create_webserver{}.use_ssl(true))` throws
`feature_unavailable`.

* **`.https_mem_key(const std::string& path)`** — load the PEM key from
  the file at `path`.
* **`.https_mem_cert(const std::string& path)`** — load the PEM
  certificate from the file at `path`.
* **`.https_mem_trust(const std::string& path)`** — load CA certificate(s)
  for verifying client certificates (mTLS).
* **`.raw_https_mem_key(const std::string& pem)`**
  / **`.raw_https_mem_cert(...)`** / **`.raw_https_mem_trust(...)`** —
  pass PEM material directly without reading a file.
* **`.https_mem_dhparams(const std::string& pem)`** — explicit DH
  parameters.
* **`.https_key_password(const std::string& pw)`** — passphrase for an
  encrypted private key.
* **`.https_priorities(const std::string& s)`** — GnuTLS priority
  string.
* **`.https_priorities_append(const std::string& s)`** — append to the
  default priority string.
* **`.cred_type(http::http_utils::cred_type_T t)`** — `CERTIFICATE` /
  `PSK` / `NONE`. Default `NONE`.
* **`.psk_cred_handler(...)`** — PSK lookup callback for TLS-PSK.
* **`.sni_callback(...)`** — Server Name Indication lookup callback
  (returns the PEM `(cert, key)` pair for a hostname).

### Authentication setup

* **`.digest_auth_random(const std::string& s)`** — server-side random
  seed for digest nonces.
* **`.nonce_nc_size(int n)`** — replay-protection window size for
  digest auth.
* **`.auth_handler(auth_handler_ptr cb)`** — install a centralized
  authentication handler that runs before every resource's `render_*`
  call. The callback returns `std::nullopt` to allow the request to
  proceed, or an `http_response` (typically
  `http_response::unauthorized(realm)`) to reject it. Single source of
  truth for auth — see [Centralized
  authentication](#using-centralized-authentication).
* **`.auth_skip_paths(const std::vector<std::string>& paths)`** — paths
  to bypass `auth_handler`. Exact match (`"/health"`) and wildcard
  suffixes (`"/public/*"`) are both supported.

### Custom error handlers

* **`.not_found_handler(error_handler h)`** — invoked when no resource
  matches the request path (HTTP 404). The handler returns
  `http_response` by value.
* **`.method_not_allowed_handler(error_handler h)`** — invoked when a
  resource matches the path but not the HTTP method (HTTP 405).
* **`.internal_error_handler(internal_error_handler_t h)`** — invoked
  when a handler throws or the library hits an internal failure during
  dispatch. The handler signature is
  `http_response(const http_request&, std::string_view message)`. Load-bearing:
  see [Error propagation](#error-propagation).
* **`.expose_exception_messages(bool = true)`** — restores the v1
  behaviour of including the originating exception message in the
  **default** 500 response body. Default is `false`: the default body
  is the fixed string `"Internal Server Error"`
  ([DR-009 Revision 1](specs/architecture/11-decisions/DR-009.md),
  CWE-209 information-disclosure fix). The configured `log_error`
  callback continues to receive the verbatim message regardless of
  this flag — only the HTTP response body is affected.
  **Warning:** exception messages routinely contain file paths, SQL
  fragments, internal identifiers, and attacker-influenced input.
  Enable only in development or behind an explicit `#ifndef NDEBUG`
  guard. A configured `internal_error_handler` is unaffected — it
  always receives the message and can build any body it wants.

A worked example:

```cpp
auto cfg = httpserver::create_webserver(8080)
    .not_found_handler([](const httpserver::http_request&) {
        return httpserver::http_response::string("nope").with_status(404);
    })
    .method_not_allowed_handler([](const httpserver::http_request&) {
        return httpserver::http_response::empty().with_status(405);
    })
    .internal_error_handler([](const httpserver::http_request&, std::string_view what) {
        // CWE-209: 'what' may contain file paths, SQL fragments, or
        // attacker-influenced input. Log it internally; do NOT echo
        // it to the HTTP client.
        (void)what;  // e.g. logger->error(what);
        return httpserver::http_response::string("Internal Server Error").with_status(500);
    });
httpserver::webserver ws{cfg};
```

### File-upload options

* **`.file_upload_target(file_upload_target_T t)`** —
  `FILE_UPLOAD_MEMORY_ONLY` (default) keeps uploads in memory;
  `FILE_UPLOAD_DISK_ONLY` writes to disk; `FILE_UPLOAD_MEMORY_AND_DISK`
  does both.
* **`.file_upload_dir(const std::string& dir)`** — directory for
  on-disk uploads. Must not be empty.
* **`.generate_random_filename_on_upload(bool = true)`** — name uploaded
  files randomly (vs. trust the client's `Content-Disposition: filename=`).
* **`.file_cleanup_callback(file_cleanup_callback_ptr cb)`** — invoked
  after request completion to clean up uploaded files. Return `true` to
  delete the file from disk, `false` to keep it.

### Daemon / external event-loop options

These dovetail with the runtime methods covered under [Daemon
introspection and external event loops](#daemon-introspection-and-external-event-loops).
The `default_policy` selects which list is the exception list — i.e. what
happens to an address on neither list:

* **`.default_policy(http::http_utils::policy_T p)`** — `ACCEPT` (default)
  admits every address except those on the deny list (`deny_ip`); `REJECT`
  refuses every address except those on the allow list (`allow_ip`). Under
  either policy an allow entry overrides a matching deny entry.

### Runtime operations on `webserver`

Once constructed, the `webserver` instance exposes:

* **`bool start(bool blocking = false)`** — start listening. When
  `blocking == true`, this thread blocks until the server stops.
* **`bool stop()`** — stop the daemon and join libmicrohttpd's worker
  threads. **Calling this from inside a handler deadlocks** — see
  [Threading contract](#threading-contract).
* **`void stop_and_wait()`** — `stop()` then wait until every in-flight
  handler has returned. Same deadlock contract as `stop()`.
* **`bool is_running()`** — true if the daemon is currently accepting
  connections.

Registration verbs (covered in detail under [Routing](#routing)):
`on_get`, `on_post`, `on_put`, `on_delete`, `on_patch`, `on_options`,
`on_head`, `route(...)`, `register_path(...)`, `register_prefix(...)`,
`register_ws_resource(...)`, and the matching `unregister_*` forms.

[Back to TOC](#table-of-contents)

## The resource object

The `http_resource` class represents a logical collection of HTTP methods
that will be associated with a URL when registered on the webserver. The
class is **designed for extension**. When the webserver matches a request
against a resource, the method corresponding to the request's HTTP verb
is called on the resource.

The `http_resource` class contains the following extensible methods
(also called *handlers* or *render methods*) — every one returns
`http_response` by value:

* **`http_response render_get(const http_request& req)`** — invoked on an HTTP GET request.
* **`http_response render_post(const http_request& req)`** — invoked on an HTTP POST request.
* **`http_response render_put(const http_request& req)`** — invoked on an HTTP PUT request.
* **`http_response render_head(const http_request& req)`** — invoked on an HTTP HEAD request.
* **`http_response render_delete(const http_request& req)`** — invoked on an HTTP DELETE request.
* **`http_response render_trace(const http_request& req)`** — invoked on an HTTP TRACE request.
* **`http_response render_options(const http_request& req)`** — invoked on an HTTP OPTIONS request.
* **`http_response render_connect(const http_request& req)`** — invoked on an HTTP CONNECT request.
* **`http_response render_patch(const http_request& req)`** — invoked on an HTTP PATCH request.
* **`http_response render(const http_request& req)`** — fallback invoked when no method-specific override is provided.

These methods are all `virtual`; override only the verbs your resource
supports. Unhandled verbs fall through to `render()`; if the resource also
does not override `render()`, the dispatch returns the configured 405
response.

### Restricting allowed methods

By default, every HTTP verb falls through to `render()`. To narrow a
resource to a specific subset of methods, use the `set_allowing` /
`disallow_all` API:

```cpp
class read_only : public httpserver::http_resource {
 public:
    read_only() {
        disallow_all();
        set_allowing("GET", true);
        set_allowing("HEAD", true);
    }
    httpserver::http_response render_get(const httpserver::http_request&) override {
        return httpserver::http_response::string("ok");
    }
};
```

Requests for disallowed verbs are short-circuited by the dispatcher and
land in the `method_not_allowed_handler` (HTTP 405) without calling
`render_*`. See [`examples/allowing_disallowing_methods.cpp`](examples/allowing_disallowing_methods.cpp)
for a worked example.

### Ownership

Resources are passed to the webserver via `std::unique_ptr` (the
webserver takes exclusive ownership) or `std::shared_ptr` (caller and
webserver share ownership). Ownership is always expressed through a
smart pointer.

[Back to TOC](#table-of-contents)

## Routing

The `webserver` exposes three families of registration entry points.
They are all interoperable: within one server, some paths can be
lambdas and others can be `http_resource` subclasses.

### Lambda form: per-method, exact path

`on_get`, `on_post`, `on_put`, `on_delete`, `on_patch`, `on_options`,
`on_head` — each takes an exact path and a
`std::function<http_response(const http_request&)>`:

```cpp
ws.on_get("/hello", [](const http_request&) {
    return http_response::string("Hello, World!");
});
ws.on_post("/items", [](const http_request& req) {
    return http_response::string("created: " + std::string{req.get_arg("name")})
        .with_status(201);
});
```

Re-registering the same `(method, path)` pair throws. The seven
shortcuts cover the seven verbs with first-class handler functions;
CONNECT and TRACE go through `route()` (below).

### Lambda form: atomic multi-method via `route()`

`route()` is the primary escape hatch when the HTTP method is known
only at runtime (e.g. loaded from config, selected from a dispatch
table). The single-method form:

```cpp
route(http_method m, "/info", handler)
```

takes a runtime `http_method` value and is the canonical form for
config-driven or table-driven registration (PRD-HDL-REQ-006).

`route(method_set methods, "/info", handler)` additionally allows
registering a handler under several methods in a single critical
section — either every slot is registered, or none of them are:

```cpp
ws.route(http_method::GET | http_method::HEAD, "/info",
         [](const http_request&) {
             return http_response::string("info");
         });
```

A `method_set` is the bitwise-or of `http_method` values. This is the
only entry point through which CONNECT and TRACE are reachable as
lambdas: `route(http_method::CONNECT, "/proxy", handler)`.

### Resource form: `register_path` and `register_prefix`

For `http_resource` subclasses (the [class form](#class-form-handlers)):

```cpp
ws.register_path("/count",   std::make_unique<counter>());   // exact match
ws.register_prefix("/static", std::make_unique<files>());    // subtree match
```

Both methods have `std::shared_ptr<http_resource>` overloads for the
case where you need to retain a reference to the resource:

```cpp
auto cnt = std::make_shared<counter>();
ws.register_path("/count", cnt);
// cnt is still usable from outside the webserver.
```

**Parameterised paths.** Brace syntax captures path segments:

```cpp
ws.register_path("/users/{id}", std::make_unique<user_resource>());
```

The captured value is available inside the handler via
`http_request::get_arg("id")`.

**Per-segment regex constraints.** Add a regex after a pipe to constrain
a segment:

```cpp
ws.register_path("/users/{id|[0-9]+}", std::make_unique<user_resource>());
```

Only requests where the `id` segment matches `[0-9]+` will match this
registration.

Regex validation is on by default; disable with `regex_checking(false)`
on the builder if you need pure literal-path semantics.

**Unregistration.** `unregister_path(path)` and `unregister_prefix(path)`
remove a previously registered resource by path. (`unregister_resource`
exists as a deprecated alias for `unregister_path`.)

## Request

`http_request` is read-only inside a handler. The accessors are designed
around `std::string_view` so reading headers and arguments does not
allocate.

### Path, method, version

| Accessor | Returns | Notes |
|---|---|---|
| `get_path()` | `std::string_view` | The decoded path |
| `get_path_pieces()` | `const std::vector<...>&` | Split path components |
| `get_method()` | `httpserver::http_method` | Strongly-typed enum; see [`http_method.hpp`](src/httpserver/http_method.hpp) |
| `get_version()` | `std::string_view` | `"HTTP/1.1"`, `"HTTP/2"`, … |
| `get_querystring()` | `std::string_view` | Raw query string (no decoding) |
| `get_connection_id()` | uintptr-style id | Stable identifier for the underlying connection |
| `get_requestor()` | `std::string_view` | Connecting peer's IP address (text form) |
| `get_requestor_port()` | `unsigned short` | Connecting peer's port |

### Headers, arguments, cookies

| Accessor | Returns | Notes |
|---|---|---|
| `get_headers()` | `const map&` | All request headers |
| `get_header(name)` | `std::string_view` | First value for a named header |
| `get_args()` | `const map&` | All query / form arguments |
| `get_arg(name)` | `std::string_view` | First value for a query / form arg |
| `get_arg_flat(name)` | `std::string_view` | Alias for `get_arg`; explicit "first value only" form |
| `get_cookies()` | `const map&` | All cookies |
| `get_cookie(name)` | `std::string_view` | First value for a named cookie |
| `get_footers()` | `const map&` | Chunked trailers |

### Uploaded files (multipart)

| Accessor | Returns | Notes |
|---|---|---|
| `get_files()` | `const map&` | Uploaded files keyed by form field name |
| `get_content()` | `std::string_view` | The raw request body |
| `get_content_size_limit()` | `size_t` | Configured cap, in bytes |

See [`examples/file_upload.cpp`](examples/file_upload.cpp) and
[`examples/file_upload_with_callback.cpp`](examples/file_upload_with_callback.cpp)
for working programs.

### Authentication accessors

| Accessor | Returns | Notes |
|---|---|---|
| `get_user()` | `std::string_view` | Basic-auth user; empty when `HAVE_BAUTH` is off |
| `get_pass()` | `std::string_view` | Basic-auth password; empty when `HAVE_BAUTH` is off |
| `get_digested_user()` | `std::string_view` | Digest-auth user; empty when `HAVE_DAUTH` is off |
| `check_digest_auth(realm, password, nonce_timeout, signal_stale, algo)` | `digest_auth_result` | Validates a digest auth response against a plaintext password |
| `check_digest_auth_digest(realm, ha1, ...)` | `digest_auth_result` | Same as above but against a pre-computed HA1 hash |

> **Security note.** Basic auth (`get_user`/`get_pass`) transmits
> credentials as Base64 — effectively cleartext. Digest auth
> (`get_digested_user`) avoids transmitting the password but is still
> vulnerable to man-in-the-middle attacks without TLS. Both are only
> safe when the server is configured with TLS (`HAVE_GNUTLS`, `.use_ssl(true)`).
> See [Feature availability](#feature-availability) and
> [`examples/basic_authentication.cpp`](examples/basic_authentication.cpp).

`digest_auth_result` is a strongly-typed enum:

* `OK` — authentication succeeded.
* `NONCE_STALE` — the nonce is stale; signal the client to retry with a fresh nonce by setting `signal_stale = true` in the response.
* `WRONG_USERNAME`, `WRONG_REALM`, `WRONG_URI`, `WRONG_QOP`, `WRONG_ALGO`, `RESPONSE_WRONG` — specific reasons for failure.
* `WRONG_HEADER`, `TOO_LARGE`, `NONCE_WRONG`, `NONCE_OTHER_COND`, `ERROR` — other failure conditions.

### TLS / client-certificate accessors

When the build was compiled with GnuTLS (`HAVE_GNUTLS`) **and** the
client presented an X.509 certificate during the TLS handshake, these
accessors return the certificate details. On a non-TLS build, or when
the client did not present a certificate, they return empty / `-1` /
`false`:

| Accessor | Returns | Notes |
|---|---|---|
| `has_client_certificate()` | `bool` | True if a client cert was presented |
| `is_client_cert_verified()` | `bool` | True if the chain validated against the configured trust store |
| `get_client_cert_dn()` | `std::string_view` | Subject Distinguished Name |
| `get_client_cert_issuer_dn()` | `std::string_view` | Issuer Distinguished Name |
| `get_client_cert_cn()` | `std::string_view` | Subject Common Name |
| `get_client_cert_fingerprint_sha256()` | `std::string_view` | Hex-encoded SHA-256 fingerprint |
| `get_client_cert_not_before()` | `time_t` | Validity-start timestamp; `-1` if unavailable |
| `get_client_cert_not_after()` | `time_t` | Validity-end timestamp; `-1` if unavailable |

See [`examples/client_cert_auth.cpp`](examples/client_cert_auth.cpp) for a
worked mTLS example.

### Lifetime contract

Every `string_view` returned by `http_request` is valid for the duration
of the handler invocation **and no longer**. Copy what you need to keep
(e.g. into a `std::string`); do not hand a view to a deferred callback.
The references returned by `get_headers()`, `get_args()`,
`get_path_pieces()`, `get_files()`, and `get_cookies()` follow the same
rule. `http_request` is **single-threaded per request**: sharing one
`http_request` across threads is undefined.

### Method enum

`http_method` (declared in
[`http_method.hpp`](src/httpserver/http_method.hpp)) covers the canonical
HTTP methods. `method_set` is a bitset used by atomic multi-method
registration on `route()` (see [Routing](#routing)).

[Back to TOC](#table-of-contents)

## Response

`http_response` is a **value type** — move-only, returned by value,
never `shared_ptr`-wrapped. There is no class hierarchy of body
subclasses; the body shape is a runtime detail of one type. Build a
response with one of the seven factories described below and decorate
it with the fluent `with_*` mutators.

### The seven factories

| Factory | Body shape | Use when |
|---|---|---|
| `http_response::string(body, [status, content_type])` | In-memory string (small bodies live inline via SBO) | The body is already in memory |
| `http_response::file(path)` | Stream a file from disk | The body is a static or generated file on disk |
| `http_response::iovec(entries)` | Scatter-gather over a vector of `iovec_entry` (zero-copy) | The body is assembled from several existing buffers |
| `http_response::pipe(fd)` | Stream from a pipe / FIFO | The body is being produced by another process or thread |
| `http_response::empty([status])` | Empty body | 204 No Content, redirects, HEAD responses |
| `http_response::deferred(producer, [closure, content_type])` | Body produced incrementally by a callback | The body cannot be materialised up-front (long-poll, streaming) |
| `http_response::unauthorized(realm, [status, content_type, algorithm])` | 401 with the proper `WWW-Authenticate` header | Reject a request that lacks valid credentials |

`iovec_entry` is the element type of the `iovec()` vector:

```cpp
struct iovec_entry {
    const void* base;   // borrowed for the response's lifetime
    size_t length;
};
```

The `base` pointer is **borrowed**: the caller must keep the underlying
storage alive until the response has been fully written to the wire.
For owning scatter-gather, copy your strings into a `std::vector<std::string>`
that the response captures by value.

### Fluent mutation

Every `http_response` exposes `with_status`, `with_header`, `with_footer`,
and `with_cookie`. These return `*this` (by reference on lvalues, by
rvalue-reference on rvalues), so calls can chain:

```cpp
return httpserver::http_response::string("hi")
    .with_header("X-Trace-Id", trace_id)
    .with_status(201);
```

The fluent helpers are intentionally narrow — every other mutation
(content type, body data) is set at the factory call. This keeps the
"build it, return it, done" idiom uniform across response shapes.

### Building error responses by value

There is **no throw-as-status idiom**. To return a 404 from a handler,
build it explicitly:

```cpp
if (!found) {
    return httpserver::http_response::empty().with_status(404);
}
```

Or with a body:

```cpp
return httpserver::http_response::string("user not found").with_status(404);
```

For 401 responses, the dedicated `http_response::unauthorized` factory
sets the right `WWW-Authenticate` header for you:

```cpp
return httpserver::http_response::unauthorized("MyRealm");
```

See [`examples/setting_headers.cpp`](examples/setting_headers.cpp),
[`examples/iovec_response_example.cpp`](examples/iovec_response_example.cpp),
[`examples/minimal_file_response.cpp`](examples/minimal_file_response.cpp),
[`examples/pipe_response_example.cpp`](examples/pipe_response_example.cpp),
[`examples/empty_response_example.cpp`](examples/empty_response_example.cpp),
[`examples/minimal_deferred.cpp`](examples/minimal_deferred.cpp), and
[`examples/binary_buffer_response.cpp`](examples/binary_buffer_response.cpp)
for working programs covering every response shape.

[Back to TOC](#table-of-contents)

## IP blocking and unblocking

libhttpserver supports both IPv4 and IPv6 and manages them transparently.
The only requirement for IPv6 is that it is enabled on the underlying
server — set `use_ipv6(true)` on `create_webserver` (or `use_dual_stack(true)`
for both stacks on the same socket).

You can populate the deny list and the allow list (with individual IPs or
wildcard ranges) at runtime using these methods on `webserver`:

* **`void deny_ip(std::string_view ip)`** — add one IP (or a range,
  e.g. `"127.0.0.*"`) to the deny list. Connections from a matching
  address are refused at the policy callback. This is the exception list
  under the default `ACCEPT` policy.
* **`void remove_denied_ip(std::string_view ip)`** — remove one IP (or a
  range) from the deny list. Idempotent: removing an entry that is not
  currently denied is a no-op.
* **`void allow_ip(std::string_view ip)`** — add one IP (or a range) to
  the allow list. This is the exception list under the `REJECT` policy
  (permit only these). Under `ACCEPT`, an allow entry overrides a
  matching `deny_ip` entry.
* **`void remove_allowed_ip(std::string_view ip)`** — remove one IP (or a
  range) from the allow list. Idempotent.

### IP string format

The IP string format can represent both IPv4 and IPv6. Addresses are
normalised internally by the webserver to a common representation, so
any valid IPv4 or IPv6 textual representation works. To express a
range, omit the octet you want to wildcard and specify `'*'` in its
place.

Examples of valid IPs include:

* `"192.168.5.5"` — standard IPv4
* `"192.168.*.*"` — range of IPv4 addresses; in the example, everything between `192.168.0.0` and `192.168.255.255`
* `"2001:db8:8714:3a90::12"` — standard IPv6; clustered empty ranges are fully supported
* `"2001:db8:8714:3a90:*:*"` — range of IPv6 addresses
* `"::ffff:192.0.2.128"` — IPv4 nested into IPv6
* `"::192.0.2.128"` — IPv4 nested into IPv6 (without the `ffff` prefix)
* `"::ffff:192.0.*.*"` — ranges of IPv4 IPs nested into IPv6

### Allow-list mode

By default (`ACCEPT` policy) the deny list is the exception list: use
`deny_ip` to refuse specific addresses and admit everyone else. To invert
this into an allow list — refuse everyone except specific addresses — set
the default policy to `REJECT` and populate the allow list with `allow_ip`:

```cpp
webserver ws{create_webserver(8080)
    .default_policy(http::http_utils::REJECT)};
ws.allow_ip("192.168.0.*");  // permits 192.168.0.0/24, refuses everything else
```

See [`examples/minimal_ip_access_control.cpp`](examples/minimal_ip_access_control.cpp)
for a worked example of both modes.

[Back to TOC](#table-of-contents)

## Authentication

libhttpserver supports four authentication mechanisms, all of which can
be combined freely (e.g. mTLS *and* digest auth in a two-factor
configuration):

* **Basic authentication** — username and password are exchanged in
  clear (base64-encoded) between the client and the server. Use only
  for non-sensitive content or under HTTPS. With Basic auth, the
  application sees the cleartext password — useful for chained
  authentication against an external store. Enable / disable with
  `basic_auth(true|false)` on the builder.
* **Digest authentication** — a one-way authentication scheme based on
  hash algorithms (MD5, SHA-256, or SHA-512/256). Only the hash transits
  the network; a server-issued nonce prevents replay attacks. Appropriate
  for general use, especially when HTTPS is not available. SHA-256 is the
  default; SHA-512/256 is also supported for stronger security. Enable /
  disable with `digest_auth(true|false)`.
* **Client-certificate authentication (mTLS)** — uses an X.509
  certificate from the client. The strongest of the three mechanisms,
  but requires HTTPS. Combines well with Basic or Digest for layered
  authentication. Enable with `use_ssl(true)` plus configured `https_mem_trust`.
* **Centralized authentication** — a single per-request callback that
  runs *before* any resource's render method, so individual handlers
  don't need to repeat auth code. Configured via `auth_handler` and
  `auth_skip_paths` on the builder.

### Using Basic authentication

```cpp
class user_pass_resource : public httpserver::http_resource {
 public:
    httpserver::http_response render_get(const httpserver::http_request& req) override {
        if (req.get_user() != "myuser" || req.get_pass() != "mypass") {
            return httpserver::http_response::unauthorized("test@example.com");
        }
        return httpserver::http_response::string(
            std::string{req.get_user()} + " " + std::string{req.get_pass()});
    }
};

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};
    ws.register_path("/hello", std::make_unique<user_pass_resource>());
    ws.start(true);
}
```

To test:

```sh
curl -XGET -v -u myuser:mypass http://localhost:8080/hello
```

You will get back the user and password you passed. Try passing wrong
credentials to see the failure response. See
[`examples/basic_authentication.cpp`](examples/basic_authentication.cpp)
for the full source.

### Using Digest authentication

`check_digest_auth` returns a `digest_auth_result` enum with fine-grained
status codes (see [Request](#request) for the full list). The skeleton
of a digest-protected handler is:

```cpp
httpserver::http_response render_get(const httpserver::http_request& req) override {
    if (req.get_digested_user().empty()) {
        return httpserver::http_response::unauthorized(
            "test@example.com", 401, "text/plain",
            httpserver::http::http_utils::digest_algorithm::SHA_256);
    }
    auto result = req.check_digest_auth(
        "test@example.com", "mypass", 300, 0,
        httpserver::http::http_utils::digest_algorithm::SHA_256);
    if (result == httpserver::http::http_utils::digest_auth_result::NONCE_STALE) {
        return httpserver::http_response::unauthorized(
            "test@example.com", 401, "text/plain",
            httpserver::http::http_utils::digest_algorithm::SHA_256)
            .with_header("Stale", "true");
    }
    if (result != httpserver::http::http_utils::digest_auth_result::OK) {
        return httpserver::http_response::unauthorized(
            "test@example.com", 401, "text/plain",
            httpserver::http::http_utils::digest_algorithm::SHA_256);
    }
    return httpserver::http_response::string("SUCCESS");
}
```

To test:

```sh
curl -XGET -v --digest --user myuser:mypass localhost:8080/hello
```

You'll get `SUCCESS` in response; observe the response message in detail
to see the full digest handshake. See
[`examples/digest_authentication.cpp`](examples/digest_authentication.cpp).

### Using centralized authentication

The per-resource pattern above duplicates auth logic across every
resource. Centralized authentication lets you define the policy once and
have it applied automatically to every request:

```cpp
// auth runs once per request before any render_*; return std::nullopt to allow
auto auth = [](const httpserver::http_request& req)
    -> std::optional<httpserver::http_response> {
    if (req.get_user() != "admin" || req.get_pass() != "secret") {
        return httpserver::http_response::unauthorized("MyRealm");
    }
    return std::nullopt;
};

httpserver::webserver ws{httpserver::create_webserver(8080)
    .auth_handler(auth)
    .auth_skip_paths({"/health", "/public/*"})};

ws.register_path("/api",    std::make_unique<api_resource>());
ws.register_path("/health", std::make_unique<health_resource>());
ws.start(true);
```

The `auth_handler` callback runs for every request before the resource's
render method. It receives the `http_request` and can:

* Return `std::nullopt` to allow the request to proceed normally.
* Return an `http_response` (typically `http_response::unauthorized`) to
  reject the request. The response is moved onto the wire by the
  dispatcher; no heap allocation is required.

`auth_skip_paths` accepts a vector of paths that should bypass the
handler:

* Exact matches: `"/health"` matches only `/health`.
* Wildcard suffixes: `"/public/*"` matches `/public/`, `/public/info`,
  `/public/docs/api`, etc.

To test (without auth — returns 401):

```sh
curl -v http://localhost:8080/api
```

With valid auth — returns 200:

```sh
curl -u admin:secret http://localhost:8080/api
```

Health endpoint (skip path) — works without auth:

```sh
curl http://localhost:8080/health
```

See [`examples/centralized_authentication.cpp`](examples/centralized_authentication.cpp).

### Using client-certificate authentication (mTLS)

Client-certificate authentication (mutual TLS, mTLS) provides strong
authentication by requiring the client to present an X.509 certificate
during the TLS handshake. To enable mTLS:

1. `use_ssl(true)` — enable TLS.
2. `https_mem_key("server_key.pem")` and `https_mem_cert("server_cert.pem")` — server certificate.
3. `https_mem_trust("ca_cert.pem")` — CA certificate(s) to verify client certificates.

```cpp
class secure : public httpserver::http_resource {
 public:
    httpserver::http_response render_get(const httpserver::http_request& req) override {
        if (!req.has_client_certificate()) {
            return httpserver::http_response::string("client certificate required")
                .with_status(401);
        }
        if (!req.is_client_cert_verified()) {
            return httpserver::http_response::string("certificate not verified")
                .with_status(403);
        }
        std::string cn{req.get_client_cert_cn()};
        return httpserver::http_response::string("Welcome, " + cn + "!");
    }
};

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8443)
        .use_ssl(true)
        .https_mem_key("server_key.pem")
        .https_mem_cert("server_cert.pem")
        .https_mem_trust("ca_cert.pem")};
    ws.register_path("/secure", std::make_unique<secure>());
    ws.start(true);
}
```

Available client-certificate methods (require GnuTLS support):

* `has_client_certificate()` — was a client cert presented?
* `is_client_cert_verified()` — did the chain validate?
* `get_client_cert_dn()` — subject Distinguished Name
* `get_client_cert_issuer_dn()` — issuer Distinguished Name
* `get_client_cert_cn()` — Common Name from the subject
* `get_client_cert_fingerprint_sha256()` — hex-encoded SHA-256 fingerprint
* `get_client_cert_not_before()` / `get_client_cert_not_after()` — validity window

Test with a client certificate:

```sh
curl -k --cert client_cert.pem --key client_key.pem https://localhost:8443/secure
```

Without a client certificate (will be rejected):

```sh
curl -k https://localhost:8443/secure
```

See [`examples/client_cert_auth.cpp`](examples/client_cert_auth.cpp).

### Server Name Indication (SNI) callback

SNI lets a server host multiple TLS certificates on a single IP address.
The client indicates which hostname it's connecting to during the TLS
handshake, and the server can select the appropriate certificate.
Configure an SNI callback that returns the `(cert_pem, key_pem)` pair for
each server name:

```cpp
std::map<std::string, std::pair<std::string, std::string>> certs;

auto sni = [](const std::string& server_name)
    -> std::pair<std::string, std::string> {
    auto it = certs.find(server_name);
    if (it != certs.end()) return it->second;
    return {"", ""};  // fall back to the default certificate
};

httpserver::webserver ws{httpserver::create_webserver(443)
    .use_ssl(true)
    .https_mem_key("default_key.pem")
    .https_mem_cert("default_cert.pem")
    .sni_callback(sni)};
```

SNI support requires libmicrohttpd 1.0.0 or later compiled with GnuTLS.

### TLS-PSK (pre-shared key)

For environments where X.509 certificates are impractical, libhttpserver
also supports TLS-PSK via `cred_type(http::http_utils::PSK)` plus a
`psk_cred_handler` callback returning the PSK for a given identity. See
[`examples/minimal_https_psk.cpp`](examples/minimal_https_psk.cpp).

[Back to TOC](#table-of-contents)

## WebSocket support

libhttpserver provides WebSocket support when libmicrohttpd is built
with WebSocket functionality. To use WebSockets, derive from the
`websocket_handler` class and implement `on_message`.

### The `websocket_handler` class

The `websocket_handler` class provides the following virtual methods:

* **`void on_open(websocket_session& session)`** — called when a new WebSocket connection is established. Default implementation does nothing.
* **`void on_message(websocket_session& session, std::string_view msg)`** — called when a text message is received. **This is the only pure virtual method and must be implemented.**
* **`void on_binary(websocket_session& session, const void* data, size_t len)`** — called when a binary message is received. Default implementation does nothing.
* **`void on_ping(websocket_session& session, std::string_view payload)`** — called when a ping frame is received. Default implementation sends a pong.
* **`void on_close(websocket_session& session, uint16_t code, const std::string& reason)`** — called when the WebSocket connection is closed. Default implementation does nothing.

### The `websocket_session` class

The `websocket_session` class provides methods to interact with the
client:

* **`void send_text(const std::string& msg)`** — send a text message.
* **`void send_binary(const void* data, size_t len)`** — send a binary message.
* **`void send_ping(const std::string& payload = "")`** — send a ping frame.
* **`void send_pong(const std::string& payload = "")`** — send a pong frame.
* **`void close(uint16_t code = 1000, const std::string& reason = "")`** — close the WebSocket connection.
* **`bool is_valid()`** — true while the session is still open.

### Registering WebSocket resources

WebSocket handlers are registered with `register_ws_resource`, which
takes ownership of a `websocket_handler` subclass via `std::unique_ptr`
(or shares it via `std::shared_ptr`):

```cpp
class echo : public httpserver::websocket_handler {
 public:
    void on_message(httpserver::websocket_session& session,
                    std::string_view msg) override {
        session.send_text("Echo: " + std::string{msg});
    }
};

int main() {
    httpserver::webserver ws{httpserver::create_webserver(8080)};
    ws.register_ws_resource("/echo", std::make_unique<echo>());
    // or, with shared ownership:
    auto handler = std::make_shared<echo>();
    ws.register_ws_resource("/echo2", handler);
    ws.start(true);
}
```

On a build with `HAVE_WEBSOCKET` disabled — for example, when the system
libmicrohttpd was built without WebSocket support — `register_ws_resource`
throws `feature_unavailable`. See [Feature
availability](#feature-availability) for how to detect this at runtime
without preprocessor gates, and
[`examples/websocket_echo.cpp`](examples/websocket_echo.cpp) for a
worked example.

[Back to TOC](#table-of-contents)

## Daemon introspection and external event loops

libhttpserver exposes several methods for integrating with external
event loops and for querying daemon state at runtime.

### Daemon introspection

* **`uint16_t webserver::get_bound_port()`** — actual port the daemon is
  bound to. Especially useful when you pass port `0` to let the OS
  choose an ephemeral port.
* **`int webserver::get_listen_fd()`** — the listen socket file
  descriptor, or `-1` if not available.
* **`unsigned int webserver::get_active_connections()`** — the number of
  currently active connections.
* **`bool webserver::is_running()`** — true if the daemon is currently
  accepting connections.

### External event-loop integration

When using the server without internal threading (e.g. with
`listen_socket(false)` or with `start_method(EXTERNAL_SELECT)`), you can
drive the event loop yourself:

* **`bool webserver::run()`** — process pending events once and return
  immediately.
* **`bool webserver::run_wait(int32_t millisec)`** — block until events
  are available or the timeout expires.
* **`bool webserver::get_fdset(void* read, void* write, void* except, int* max_fd)`**
  — retrieve file descriptor sets for use with `select(2)`.
* **`bool webserver::get_timeout(uint64_t* timeout)`** — the maximum
  time to wait before calling `run()` again.
* **`bool webserver::add_connection(int socket, const sockaddr* addr, socklen_t len)`**
  — hand off an externally-accepted connection to the daemon. Used with
  socket-activation or load-balancer integrations.
* **`int webserver::quiesce()`** — stop accepting new connections while
  allowing in-flight requests to complete. Returns the listen socket FD,
  which the caller can then close or pass elsewhere.

### Stopping the daemon

* **`bool webserver::stop()`** — stop the daemon, join libmicrohttpd's
  worker threads, and return. **Calling `stop()` from inside a handler
  deadlocks** (it joins the calling thread); see [Threading
  contract](#threading-contract).
* **`void webserver::stop_and_wait()`** — equivalent to `stop()` plus a
  drain that waits for every in-flight handler to return. Same deadlock
  contract as `stop()`.

A small example combining everything:

```cpp
// Bind to an OS-chosen port, print the actual one, drive the loop
// externally, then drain on shutdown.
webserver ws{create_webserver(0).start_method(http::http_utils::EXTERNAL_SELECT)};
ws.start(false);
std::cout << "listening on port " << ws.get_bound_port() << '\n';
while (running) {
    ws.run_wait(1000);
}
ws.quiesce();
ws.stop_and_wait();
```

See [`examples/daemon_info.cpp`](examples/daemon_info.cpp) and
[`examples/external_event_loop.cpp`](examples/external_event_loop.cpp)
for runnable demonstrations.

[Back to TOC](#table-of-contents)

## HTTP utils

libhttpserver provides a set of constants and helpers to help you build
your HTTP server. The full list of named constants (status codes, common
methods, common content types, named algorithms) lives in
[`src/httpserver/http_utils.hpp`](src/httpserver/http_utils.hpp) — it
would be redundant to enumerate it here.

The following utility functions are available on `http::http_utils`:

* **`const char* reason_phrase(unsigned int status_code)`** — the
  standard HTTP reason phrase for a given status code (e.g. `"OK"` for
  200, `"Not Found"` for 404).
* **`bool is_feature_supported(int feature)`** — whether a specific
  libmicrohttpd feature is supported on the current system. Feature
  constants are defined by the `MHD_FEATURE` enum.
* **`const char* get_mhd_version()`** — the version string of the
  underlying libmicrohttpd library.

[Back to TOC](#table-of-contents)

## Lifecycle hooks

The hook bus is the single extension surface for observing or
short-circuiting the request lifecycle. It replaces v1's single-slot
patchwork (one `log_access` callback, one `not_found_handler`, one
`method_not_allowed_handler`, one `internal_error_handler`, one
`auth_handler`) with eleven distinct phases spanning connection,
request, routing, handler, and response. Each phase accepts multiple
subscribers and is observable both server-wide (via `webserver::add_hook`)
and, for the five post-route-resolution phases, per-route (via
`http_resource::add_hook`). The contract is captured in DR-012 and
[`specs/architecture/04-components/hooks.md`](specs/architecture/04-components/hooks.md).

Each call returns a move-only `hook_handle`. The handle's destructor
removes the registration; `hook_handle::detach()` disarms the destructor
so the registration persists for the webserver's lifetime.

### Phases

| Phase | Fires at | Short-circuit | Per-route eligible |
|-------|----------|---------------|--------------------|
| `connection_opened` | New TCP / TLS connection accepted by MHD | No | No |
| `accept_decision` | After the default-policy / deny-list / allow-list verdict; the connection has been accepted or denied | No | No |
| `request_received` | Request line and headers parsed, body not yet consumed | Yes (`hook_action`) | No |
| `body_chunk` | Each upload-body chunk delivered by MHD | Yes (`hook_action`) | No |
| `route_resolved` | After URL → resource resolution; carries the matched route or "no match" | No | No |
| `before_handler` | After route resolution and method check, immediately before the handler runs | Yes (`hook_action`) | Yes |
| `handler_exception` | Exception escapes the handler, before `internal_error_handler` is consulted | Yes (`hook_action`) | Yes |
| `after_handler` | Handler returned a response, before it is queued on the wire (mutation point) | Yes (`hook_action`) | Yes |
| `response_sent` | Response handed to `MHD_queue_response`; carries `status`, `bytes_queued`, `elapsed` | No | Yes |
| `request_completed` | Request lifecycle finished (success or failure); last hook to fire per request | No | Yes |
| `connection_closed` | Connection torn down (peer close or server close) | No | No |

### Short-circuit semantics

Phases marked "Short-circuit" return a `hook_action`:
`hook_action::pass()` lets the chain continue;
`hook_action::respond_with(response)` aborts the chain at that
position. The wrapped response is sent on the wire in place of any
handler output. Subsequent hooks in the same phase are not invoked.
Observation-only phases (`connection_opened`, `accept_decision`,
`connection_closed`, `route_resolved`, `response_sent`,
`request_completed`) ignore the return; the chain always runs to
completion.

### Per-route hooks

`http_resource::add_hook(phase, fn)` accepts only the five
post-route-resolution phases: `before_handler`, `handler_exception`,
`after_handler`, `response_sent`, `request_completed`. Per-route hooks
fire after the server-wide chain at the same phase, and only if that
server-wide chain did not short-circuit. Passing any other phase
throws `std::invalid_argument` naming the rejected phase. See
[`examples/per_route_auth.cpp`](examples/per_route_auth.cpp) for a
worked example.

### Examples

* [`examples/denied_ip_log.cpp`](examples/denied_ip_log.cpp) — log every
  denied-IP rejection via a single `accept_decision` hook.
* [`examples/early_413.cpp`](examples/early_413.cpp) — return 413 from
  a `request_received` hook before the body is consumed.
* [`examples/clf_access_log.cpp`](examples/clf_access_log.cpp) — write
  a Common Log Format access line from a `response_sent` hook.
* [`examples/per_route_auth.cpp`](examples/per_route_auth.cpp) —
  per-route HTTP Basic auth via `http_resource::add_hook(before_handler, ...)`.

### v1 aliases

Each of the v1 single-slot setters is an alias for an `add_hook` call
at the corresponding phase. The aliases survive for ergonomic call
sites; new code can use either form.

| v1 setter | Equivalent `add_hook` call |
|-----------|----------------------------|
| `log_access(fn)` | `ws.add_hook(hook_phase::response_sent, fn)` |
| `not_found_handler(fn)` | `ws.add_hook(hook_phase::route_resolved, fn)` |
| `method_not_allowed_handler(fn)` | `ws.add_hook(hook_phase::before_handler, fn)` |
| `internal_error_handler(fn)` | `ws.add_hook(hook_phase::handler_exception, fn)` |
| `auth_handler(fn)` | `ws.add_hook(hook_phase::before_handler, fn)` |

The aliases install observation-stub hooks under the same dispatch
plumbing, so the on-the-wire behaviour is identical regardless of
which form the caller used.

[Back to TOC](#table-of-contents)

## Threading contract

Distilled from
[`specs/architecture/05-cross-cutting.md`](specs/architecture/05-cross-cutting.md)
§5.1 and DR-008
([`specs/architecture/11-decisions/DR-008.md`](specs/architecture/11-decisions/DR-008.md)):

1. **Public methods on `webserver` are thread-safe and re-entrant from
   inside a handler.** Two exceptions: `stop()` and `~webserver()`
   **deadlock** if invoked from a handler thread, because they wait for
   that very thread to drain. `stop_and_wait()` delegates to `stop()`
   and carries the same deadlock risk. Stop the server from a different
   thread, or signal an external stop loop. v2 provides both `stop()`
   (signal and return) and `stop_and_wait()` (signal and drain until
   all in-flight handlers finish).
2. **Handlers run concurrently on libmicrohttpd worker threads.** The
   same lambda or `http_resource` instance is invoked from many threads
   at once. Any state you share — counters, caches, file handles — must
   be synchronised on your side. The library does not synchronise user
   state for you.
3. **`http_request` is single-threaded per request.** Sharing one
   `http_request` across threads is undefined; the per-request arena
   makes no guarantees outside the calling thread.
4. **`http_response` is value-typed with exclusive ownership.**
   Returning it transfers ownership into the dispatcher. There is no
   shared mutable response object.
5. **User-supplied callbacks** — `log_access`, `log_error`,
   `not_found_handler`, `method_not_allowed_handler`,
   `internal_error_handler`, `file_cleanup_callback`, PSK / SNI / ALPN
   callbacks, and any registered `render_*` — may run concurrently on
   multiple threads. Implementations MUST be thread-safe.

[Back to TOC](#table-of-contents)

## Error propagation

Distilled from
[`specs/architecture/05-cross-cutting.md`](specs/architecture/05-cross-cutting.md)
§5.2 and DR-009
([`specs/architecture/11-decisions/DR-009.md`](specs/architecture/11-decisions/DR-009.md)):

1. **A handler that throws `std::exception` is caught.** The library
   logs the exception via the configured `log_error` callback and then
   invokes `internal_error_handler(request, e.what())`. The response
   returned by `internal_error_handler` is sent to the client; if no
   custom `internal_error_handler` is configured, a default 500 with
   the **fixed body `"Internal Server Error"`** is sent
   (DR-009 Revision 1, CWE-209 fix — exception text often contains
   file paths, SQL fragments, or attacker-influenced input and must
   not cross a process boundary to an untrusted client). To restore
   the v1 behaviour of including the exception message in the body
   for development, set `.expose_exception_messages(true)` on the
   builder. The verbatim message is still surfaced via the
   `log_error` callback and to any configured
   `internal_error_handler` regardless of the flag.
2. **A handler that throws something other than `std::exception`** is
   also caught, with `"unknown exception"` substituted for the message.
   The same default body (`"Internal Server Error"`) applies; the
   `"unknown exception"` sentinel reaches the wire only when
   `.expose_exception_messages(true)` is set.
3. **Library-internal failures during dispatch** (allocation, body
   materialisation) flow through the same `internal_error_handler` path.
4. **If `internal_error_handler` itself throws**, the library logs and
   sends a hardcoded 500 with an empty body. There is no third level of
   fallback.
5. **`feature_unavailable` is a normal `std::runtime_error`** — no
   special status mapping. Catch it explicitly if you want to map it to
   a 503 or similar; the library does not.
6. **There is no throw-as-status idiom.** A handler that wants to
   return 404, 400, etc. builds the response by value (see
   [Response](#response)):
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
        // CWE-209: 'what' may contain file paths, SQL fragments, or
        // attacker-influenced input. Log it internally; do NOT echo
        // it to the HTTP client.
        (void)what;  // e.g. logger->error(what);
        return httpserver::http_response::string("Internal Server Error").with_status(500);
    });
httpserver::webserver ws{cfg};
```

See [`examples/custom_error.cpp`](examples/custom_error.cpp) for a
worked example. Note that the snippet above — which echoes `what`
verbatim to the wire — is illustrative of the explicit-handler case;
the **default** (no `internal_error_handler` configured) path now
sends a fixed body, and only the `log_error` callback receives the
verbatim message. To restore the v1 verbose-body default for
development, chain `.expose_exception_messages(true)` on the builder
(see [Custom error handlers](#custom-error-handlers)).

[Back to TOC](#table-of-contents)

## Feature availability

Several capabilities are gated by build-time flags. v2.0 makes the
gating visible at the API level so application code does not need
preprocessor guards on `HAVE_*` macros.

| Build flag | When disabled | Public-API behavior |
|---|---|---|
| `HAVE_BAUTH` | Basic-auth disabled | `get_user`, `get_pass` return empty `string_view`; `features().basic_auth == false`; `create_webserver::basic_auth(true)` throws `feature_unavailable` at `webserver` construction |
| `HAVE_DAUTH` | Digest-auth disabled | `get_digested_user` returns empty; `check_digest_auth` returns a sentinel result; `features().digest_auth == false`; `create_webserver::digest_auth(true)` throws `feature_unavailable` |
| `HAVE_GNUTLS` | TLS disabled | All `get_client_cert_*` accessors return empty / `-1` / `false`; `features().tls == false`; `create_webserver::use_ssl(true)` throws `feature_unavailable` |
| `HAVE_WEBSOCKET` | WebSocket disabled | `register_ws_resource` throws `feature_unavailable`; `features().websocket == false` |

### Probing at runtime

`webserver::features()` returns a small struct of four `bool`s — one
per flag — so callers can branch without preprocessor help:

```cpp
if (ws.features().tls) {
    // safe to read client-certificate accessors on this build
}
if (ws.features().websocket) {
    ws.register_ws_resource("/sock", std::make_unique<my_socket>());
}
```

### `feature_unavailable`

Derives from `std::runtime_error`. Its `what()` names both the disabled
feature and the build flag that gates it, so log lines pinpoint which
flag a deployment is missing. Catch it where you call a feature-gated
method:

```cpp
try {
    ws.register_ws_resource("/sock", std::make_unique<my_socket>());
} catch (const httpserver::feature_unavailable& e) {
    std::cerr << "websocket support is not available: " << e.what() << '\n';
}
```

Deny lists, IP-allow handling, and similar features that do not depend
on external libraries are always available: `webserver::deny_ip(addr)` /
`webserver::remove_denied_ip(addr)` and `webserver::allow_ip(addr)` /
`webserver::remove_allowed_ip(addr)` install and clear per-server access
rules at runtime regardless of build flags.

[Back to TOC](#table-of-contents)

## Migrating from v1

If you're porting an application from the v1 line, the rename / removed /
added cheat sheet — every API surface that moved — lives in
[`RELEASE_NOTES.md`](RELEASE_NOTES.md). The packaging is
parallel-installable across major versions, so applications can link
the major they need side by side while porting.

[Back to TOC](#table-of-contents)

## Examples

Every example is a standalone program under [`examples/`](examples/). A
fully grouped index — with one-line descriptions of every binary — lives
in [`examples/README.md`](examples/README.md). The summary below points
to the canonical example for each topic covered in this manual.

### Start here

* [`examples/hello_world.cpp`](examples/hello_world.cpp) — ten lines, lambda
  form, no subclass, no raw pointer. Reproduced byte-for-byte at the top
  of this README.
* [`examples/shared_state.cpp`](examples/shared_state.cpp) — when the
  class form is the right shape: an `http_resource` subclass with a
  mutex-guarded counter shared between `render_get` and `render_post`.

### HTTP basics

* [`examples/setting_headers.cpp`](examples/setting_headers.cpp) — fluent
  `with_header` chaining.
* [`examples/hello_with_get_arg.cpp`](examples/hello_with_get_arg.cpp) —
  read a query-string parameter from `http_request::get_arg`.
* [`examples/args_processing.cpp`](examples/args_processing.cpp) —
  iterate every form / query argument.
* [`examples/url_registration.cpp`](examples/url_registration.cpp) —
  exact paths, prefix matches, parameterised segments, per-segment regex.
* [`examples/handlers.cpp`](examples/handlers.cpp) — distinct lambdas
  for GET and POST on the same path; the dispatcher composes them.
* [`examples/allowing_disallowing_methods.cpp`](examples/allowing_disallowing_methods.cpp)
  — narrow a resource to a specific subset of HTTP verbs.
* [`examples/custom_error.cpp`](examples/custom_error.cpp) — install
  `not_found_handler`, `method_not_allowed_handler`, and `internal_error_handler`.

### Response shapes

* [`examples/minimal_file_response.cpp`](examples/minimal_file_response.cpp) —
  stream a file from disk via `http_response::file`.
* [`examples/binary_buffer_response.cpp`](examples/binary_buffer_response.cpp) —
  return a binary buffer (e.g. a PNG) from memory.
* [`examples/iovec_response_example.cpp`](examples/iovec_response_example.cpp) —
  gather a response body from multiple borrowed buffers without copying.
* [`examples/pipe_response_example.cpp`](examples/pipe_response_example.cpp) —
  stream from a pipe filled by a background thread.
* [`examples/empty_response_example.cpp`](examples/empty_response_example.cpp) —
  bodyless responses for DELETE and HEAD.
* [`examples/minimal_deferred.cpp`](examples/minimal_deferred.cpp) — body
  produced asynchronously by a callback.
* [`examples/deferred_with_accumulator.cpp`](examples/deferred_with_accumulator.cpp) —
  deferred response that mutates shared state across calls.

### TLS and authentication

* [`examples/minimal_https.cpp`](examples/minimal_https.cpp) — enable
  TLS with PEM key / cert files.
* [`examples/minimal_https_psk.cpp`](examples/minimal_https_psk.cpp) —
  TLS with pre-shared keys.
* [`examples/basic_authentication.cpp`](examples/basic_authentication.cpp) —
  per-request HTTP Basic auth inside the handler.
* [`examples/centralized_authentication.cpp`](examples/centralized_authentication.cpp) —
  server-wide `auth_handler` and `auth_skip_paths`.
* [`examples/digest_authentication.cpp`](examples/digest_authentication.cpp) —
  per-request HTTP Digest auth via `check_digest_auth`.
* [`examples/client_cert_auth.cpp`](examples/client_cert_auth.cpp) —
  mutual TLS with X.509 client certificates.

### Lifecycle hooks

* [`examples/denied_ip_log.cpp`](examples/denied_ip_log.cpp) — log every
  denied-IP rejection from a single `accept_decision` hook (issue #332).
* [`examples/early_413.cpp`](examples/early_413.cpp) — short-circuit
  oversized uploads with 413 before any body bytes are consumed via a
  `request_received` hook (issue #273).
* [`examples/clf_access_log.cpp`](examples/clf_access_log.cpp) —
  Common Log Format access logger written as a `response_sent` hook
  using the structured `status` / `bytes_queued` / `elapsed` context
  fields (issues #281 and #69).
* [`examples/per_route_auth.cpp`](examples/per_route_auth.cpp) —
  per-route HTTP Basic auth via `http_resource::add_hook(before_handler,
  ...)` that does not touch sibling routes (DR-012).

### Operations

* [`examples/daemon_info.cpp`](examples/daemon_info.cpp) — introspect
  the running daemon (bound port, listen FD, active connections).
* [`examples/external_event_loop.cpp`](examples/external_event_loop.cpp) —
  drive `EXTERNAL_SELECT` from the application's loop via `run_wait`.
* [`examples/custom_access_log.cpp`](examples/custom_access_log.cpp) —
  server-wide access-log callback.
* [`examples/minimal_ip_access_control.cpp`](examples/minimal_ip_access_control.cpp) —
  `deny_ip` / `allow_ip` under the `ACCEPT` and `REJECT` policies.
* [`examples/turbo_mode.cpp`](examples/turbo_mode.cpp) — turbo,
  suppressed Date header, fastopen queue, listen backlog.
* [`examples/service.cpp`](examples/service.cpp) — kitchen-sink
  reference example: CLI args, optional TLS, every `render_*` override.

### File uploads

* [`examples/file_upload.cpp`](examples/file_upload.cpp) — multipart
  upload through `http_request::get_files`.
* [`examples/file_upload_with_callback.cpp`](examples/file_upload_with_callback.cpp) —
  upload with a `file_cleanup_callback`.

### WebSockets

* [`examples/websocket_echo.cpp`](examples/websocket_echo.cpp) —
  `websocket_handler` subclass registered via `register_ws_resource`.

### Benchmarks

* [`examples/benchmark_select.cpp`](examples/benchmark_select.cpp),
  [`examples/benchmark_threads.cpp`](examples/benchmark_threads.cpp),
  [`examples/benchmark_nodelay.cpp`](examples/benchmark_nodelay.cpp) —
  micro-benchmarks.

[Back to TOC](#table-of-contents)

## Copying

This manual is for libhttpserver, a C++ library for creating an embedded
RESTful HTTP server (and more).

> Permission is granted to copy, distribute and/or modify this document
> under the terms of the GNU Free Documentation License, Version 1.3
> or any later version published by the Free Software Foundation;
> with no Invariant Sections, no Front-Cover Texts, and no Back-Cover
> Texts. A copy of the license is included in the file [`LICENSE`](LICENSE).

The library itself is distributed under the GNU Lesser General Public
License v2.1 or later — see [`COPYING.LESSER`](COPYING.LESSER) for the
full text.

[Back to TOC](#table-of-contents)

## Thanks

libhttpserver builds on the work of the GNU libmicrohttpd developers and
the many contributors to libhttpserver itself (see the project's git
history for the complete list). Particular thanks to everyone who has
filed issues, sent patches, and stress-tested the v2.0 redesign.

If libhttpserver is useful to you, consider sponsoring continued
maintenance: <https://ko-fi.com/F1F5HY8B>.

[Back to TOC](#table-of-contents)
