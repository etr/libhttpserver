<!---
Copyright (C)  2011-2026  Sebastiano Merlino.
    Permission is granted to copy, distribute and/or modify this document
    under the terms of the GNU Free Documentation License, Version 1.3
    or any later version published by the Free Software Foundation;
    with no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.
    A copy of the license is included in the LICENSE file at the repository
    root, and online at <https://www.gnu.org/licenses/fdl-1.3.html>.
-->

# libhttpserver

![GA: Build Status](https://github.com/etr/libhttpserver/actions/workflows/verify-build.yml/badge.svg?branch=v3)
[![codecov](https://codecov.io/gh/etr/libhttpserver/branch/v3/graph/badge.svg)](https://codecov.io/gh/etr/libhttpserver)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1bd1e8c21f66400fb70e5a5ce357b525)](https://www.codacy.com/gh/etr/libhttpserver/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=etr/libhttpserver&amp;utm_campaign=Badge_Grade)

[![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-ff69b4?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/etr)

## Tl;dr

libhttpserver is a C++20 library for embedding HTTP servers in your application.
It owns its HTTP/1, HTTP/2, HTTP/3 and WebSocket engines. Handlers are callables:
return a bounded response value for simple work, or use a coroutine to control
body admission, streaming and upgrades.

**Features:**

- HTTP/1.0 and HTTP/1.1 over TCP, with managed or application-owned event loops.
- Optional TLS, certificate/SNI stores, initial client authentication and atomic
  credential rotation through a private OpenSSL provider.
- HTTP/2 over TLS TCP with ALPN `h2`; HTTP/3 over explicitly configured TLS UDP
  with QUICv1 and ALPN `h3`.
- One route table shared by enabled protocols, with named path captures,
  method sets, extension methods and prefix routes.
- Pull-based request bodies, backpressured response writers, reusable immutable
  response definitions and explicit ownership of borrowed/file/pipe sources.
- Basic and Digest authentication policies, bounded form and multipart adapters,
  live peer access policies, WebSocket sessions and lifecycle hooks.
- Hierarchical resource budgets, typed protocol capacities, deadlines,
  cancellation, graceful drain tickets and bounded diagnostics.

Here is a complete plaintext server. Save it as `hello.cpp`; the build and
consumer commands follow below. It serves `/hello` until you press Enter.

```cpp
#include <httpserver.hpp>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

int main() {
    namespace http = httpserver::http;
    namespace srv = httpserver::server;
    srv::server_options options;
    options.concurrency().workers = 2;
    options.add_listener({"127.0.0.1", 8080, false});
    srv::native_server server(options);
    const auto registered = server.route_sync(
        http::method::known(http::method_id::get), "/hello",
        [](const http::request_head&, std::span<const std::byte>) {
            srv::sync_response response;
            response.status = http::status::from_code(200);
            response.fields.append("Content-Type", "text/plain");
            constexpr std::string_view text = "Hello, World!\n";
            const auto bytes = std::as_bytes(std::span(text));
            response.body.assign(bytes.begin(), bytes.end());
            return response;
        }, 1024);
    if (!registered.ok()) {
        std::cerr << registered.message() << '\n';
        return 1;
    }
    const auto listened = server.listen();
    if (!listened.ok()) {
        std::cerr << listened.message() << '\n';
        return 1;
    }
    std::cout << "http://127.0.0.1:" << server.get_bound_port(0)
              << "/hello (press Enter to stop)" << std::endl;
    std::string line;
    std::getline(std::cin, line);
    server.stop();
}
```

## Table of contents

- [Introduction](#introduction)
- [Requirements](#requirements)
- [Building and installation](#building-and-installation)
- [Getting started: Hello, world](#getting-started-hello-world)
- [Callable handlers and shared state](#callable-handlers-and-shared-state)
- [Structures and classes](#structures-and-classes)
- [Create and work with a server](#create-and-work-with-a-server)
- [Resource ownership](#resource-ownership)
- [Routing](#routing)
- [Request](#request)
- [Response](#response)
- [IP access control](#ip-access-control)
- [Authentication and TLS](#authentication-and-tls)
- [WebSocket support](#websocket-support)
- [Server introspection and external event loops](#server-introspection-and-external-event-loops)
- [HTTP utilities](#http-utilities)
- [Lifecycle hooks](#lifecycle-hooks)
- [Threading contract](#threading-contract)
- [Error propagation](#error-propagation)
- [Feature availability](#feature-availability)
- [Examples](#examples)
- [Community](#community)
- [Copying](#copying)
- [Thanks](#thanks)

## Introduction

libhttpserver handles HTTP transport and request lifecycles while your
application supplies routes and policy. The installed `<httpserver.hpp>`
umbrella includes the native API; you may also include individual headers under
[`src/httpserver/`](src/httpserver/). Non-template operations are compiled into
`libhttpserver`, so consumers must link the library.

The API separates a request head from its body. A coroutine receives an
`exchange&` as soon as the complete head is routed. It can reject immediately,
admit and consume the body, suspend while application work completes, send a
response, or upgrade to a WebSocket. A bounded synchronous adapter collects a
small body and invokes a value-returning function. Both forms use the same
routing and protocol machinery.

Ownership and capacity are explicit. A request belongs to its handler task;
response definitions are immutable and may be shared subject to their source's
replay contract; network queues and protocol state have bounded capacities.
Expected failures carry `http::outcome`, and a handler exception is contained at
the request boundary. Shutdown initiation is safe inside handlers; joining work
belongs to a controlling thread.

This manual teaches ordinary use in that order. The
[native API guide](docs/v3-api.md),
[protocol capacity reference](docs/v3-protocol-limits.md), and
[native architecture](specs/architecture/v3/architecture.md) provide additional
detail. See [release notes](RELEASE_NOTES.md) for release information.

[Back to TOC](#table-of-contents)

## Requirements

The plaintext build requires a C++20 compiler and standard library, threads,
and the platform's socket facilities. Building a source checkout also requires
Autoconf, Automake, Libtool, Make and pkg-config. `./bootstrap` generates the
configure script and Makefile inputs. Doxygen is optional for generated API
documentation; libcurl is used by tests and is not a library dependency.

TLS is optional and disabled by default. The TLS build requires genuine stable
**OpenSSL 3.5.9 or later within the 3.5 release line**, including the linkable
QUIC TLS callback API. Configure checks headers, library identity and the
required provider operations. The provider is private to the library: no
OpenSSL handles appear in the application-facing HTTP API.

The source includes Linux, BSD/macOS and Windows networking implementations.
Choose a toolchain with the required C++20 library facilities; platform build
and runtime verification is performed in the corresponding CI lanes.

[Back to TOC](#table-of-contents)

## Building and installation

Use an out-of-tree Autotools build. From the checkout root:

```sh
./bootstrap
mkdir -p build/plain
cd build/plain
../../configure --disable-v3-tls --enable-examples --prefix=/usr/local
make -j2
make check
sudo make install
```

`--enable-examples` includes the fifteen [example targets](#examples).
`make -C examples` builds them from the configured build directory.
`make dist` creates a source archive. A release archive already contains the
generated configure script, so its build can begin at configure.

### Linking an application

After installation, compile the complete opening program with:

```sh
c++ -std=c++20 hello.cpp $(pkg-config --cflags --libs libhttpserver) -o hello
c++ -std=c++20 hello.cpp $(pkg-config --cflags --static --libs libhttpserver) \
    -o hello-static
```

Set `PKG_CONFIG_PATH` to `PREFIX/lib/pkgconfig` for a custom installation. Make
sure the shared library is also discoverable by the platform's runtime loader
(for example through an installed loader path or `LD_LIBRARY_PATH` on Linux).
The `--static` pkg-config form adds private thread, platform socket and selected
provider dependencies. It does not itself force archive selection: to use the
second command for a static-library consumer, select a static-only installation
or the toolchain's archive-selection flags. With both library forms installed,
the linker may still select the shared object.

Version 3.0.0 uses SOVERSION 3 (`libhttpserver.so.3` on ELF systems). Headers,
`libhttpserver.pc`, static archives and the unversioned development library name
are shared installation names. Use separate prefixes when keeping different
development installations.

### Configure options

Run `../../configure --help` in the build directory for all options. Useful
library options include:

| Option | Purpose |
| --- | --- |
| `--enable-v3-tls` / `--disable-v3-tls` | TLS provider; default off |
| `--enable-examples` / `--disable-examples` | Examples; default on |
| `--enable-debug` | Enable debug compilation; default off |
| `--enable-static` / `--disable-static` | Static archive |
| `--enable-shared` / `--disable-shared` | Shared library |
| `--disable-doxygen-doc` | Skip generated API documentation |
| `--enable-same-directory-build` | Permit an in-tree build |

### Optional TLS build

Use a separate build directory and make the selected OpenSSL installation
visible to pkg-config:

```sh
mkdir -p build/tls
cd build/tls
PKG_CONFIG_PATH=/opt/openssl-3.5/lib/pkgconfig \
    ../../configure --enable-v3-tls --enable-examples \
    --prefix=/opt/libhttpserver
make -j2
sudo make install
```

Adjust `lib` to the provider's actual library directory. For explicit selection,
configure accepts `V3_TLS_CFLAGS` and `V3_TLS_LIBS`. Supply both with flags for
the same installation, including the runtime search path when needed. See the
[TLS build guide](docs/native-tls-build.md) for provider verification and
commands. A TLS-enabled binary still needs valid published credentials and a
TLS listener configuration before it can accept encrypted traffic.

### Building on Windows with MSYS2

Use the MINGW64 shell and a MinGW-w64 C++20 toolchain for native Windows
applications. Install its compiler, Make, Libtool and pkg-config together with
the MSYS Autotools tools, then follow the out-of-tree plaintext build above.
Add the matching curl development package if running tests. Configure recognizes
MinGW and MSYS hosts; MSYS builds depend on `msys-2.0.dll`. MinGW applications
still need the runtime libraries required by their selected compiler.

Libtool produces a static `.a` archive, a DLL and a `.dll.a` import library when
the respective build forms are enabled. DLL naming follows Libtool's host
convention. This Autotools workflow uses GCC-compatible toolchains. Use
pkg-config with `-std=c++20` for consumers: private metadata includes pthread
and Windows networking/crypto libraries (`ws2_32`, `mswsock`, `bcrypt`). TLS
requires a matching supported OpenSSL installation on this platform too.

[Back to TOC](#table-of-contents)

## Getting started: Hello, world

The [opening program](#tldr) constructs `server_options`, adds a loopback TCP
listener and passes a copy of the options to `native_server`. Port 8080 is fixed;
use port 0 to let the OS choose an available port and inspect it through
`get_bound_port(0)` after listening.

`route_sync(method, pattern, handler, body_cap)` registers a bounded handler.
It receives `const http::request_head&` and `std::span<const std::byte>` and
returns `server::sync_response`: status, fields and owned body bytes. The body
cap must be at least 1. A body exceeding the declared cap produces 413 without
invoking the function; a bodyless request passes an empty span. The adapter
supplies `Content-Length` when the returned fields have no explicit framing.

Register all routes before `listen()`, check registration and startup outcomes,
and keep the server alive while requests are being handled. `listen()` starts
managed hosting and returns; it does not occupy the main thread. The example
waits on stdin and calls `stop()` from that controlling thread, which joins
engine tasks and workers before local state is destroyed.

In another terminal:

```sh
curl -v http://127.0.0.1:8080/hello
```

The response is `Hello, World!`. A HEAD route must be registered explicitly if
you want HEAD requests to succeed; GET registration alone does not provide it.
The [routes example](examples/routes.cpp) demonstrates GET and HEAD together.

[Back to TOC](#table-of-contents)

## Callable handlers and shared state

Choose the synchronous form for quick computation over a bounded complete
body. It runs to completion on a worker; blocking database or filesystem work
occupies that worker. Use `route()` with `task<void>(exchange&)` for early
admission decisions, incremental input/output, application waits and upgrades.
Tasks are lazy: `co_await` composes them, and the server owns the routed task's
execution and lifetime.

A class can hold shared application state without inheriting a library base.
Capture a `shared_ptr` in each route that uses it, and synchronize mutable state.
This fragment belongs before `server.listen()` in the opening program and
requires `<atomic>` and `<memory>`:

```cpp
struct counter_state {
    std::atomic<unsigned> count{0};
};
auto state = std::make_shared<counter_state>();
const auto registered_counter = server.route_sync(
    http::method::known(http::method_id::post), "/count",
    [state](const http::request_head&, std::span<const std::byte>) {
        srv::sync_response response;
        response.status = http::status::from_code(200);
        const std::string text = std::to_string(++state->count);
        const auto bytes = std::as_bytes(std::span(text));
        response.body.assign(bytes.begin(), bytes.end());
        return response;
    }, 1024);
if (!registered_counter.ok()) return 1;
```

Move-only handler captures are supported by `route_handler` and
`sync_route_handler`. A registered callable may run concurrently for different
requests. Capturing an object by reference does not extend its lifetime: keep
it alive until `stop()` has returned. Never retain a handler's `exchange&` in
another thread; exchange decisions and body operations belong to its executor.
Use owning resume/cancellation handles for cross-thread signaling.

[Back to TOC](#table-of-contents)

## Structures and classes

| Type | Role |
| --- | --- |
| `server::native_server` | Owns listeners/engines/routes; noncopyable |
| `server::server_options` | Listener, protocol, limit and TLS configuration |
| `server::route_handler` | Move-only `task<void>(exchange&)` callable |
| `server::sync_response` | Bounded handler response value |
| `exchange` | Request head, body, writer, decisions and cancellation |
| `http::request_head` | Target, routing path, method, protocol and fields |
| `http::method`, `http::method_set`, `http::status` | Methods and statuses |
| `http::fields` | Ordered, repeat-preserving field collection |
| `body_reader`, `response_writer` | Request input and response output |
| `response_definition`, `response_overlay` | Shared response and send fields |
| `http::outcome` | Typed operation status with a diagnostic message |
| `websocket::session` | Owning upgraded connection/stream handle |
| `server::hook_handle`, `server::drain_ticket` | Hook and drain ownership |

Most server-related types live in `httpserver::server`, semantic HTTP values in
`httpserver::http`, and tasks/exchanges/body types in `httpserver`. Fragments
below use `namespace http = httpserver::http;` and
`namespace srv = httpserver::server;`. They show the surrounding handler or
configuration function where needed; only the opening hello is a complete
standalone program.

[Back to TOC](#table-of-contents)

## Create and work with a server

Configure the complete server before constructing it. `add_listener()` appends
an endpoint and defers validation. Addresses are numeric IPv4/IPv6 literals;
empty or `"*"` means any local address. DNS resolution belongs to the application.
Listeners default to TCP, port 0 and plaintext. Set `transport` to
`listener_transport::udp` explicitly for HTTP/3.

This configuration fragment replaces the opening program's options setup:

```cpp
srv::server_options options;
options.add_listener({"127.0.0.1", 8080, false});
options.concurrency().workers = 4;
options.timeouts().header = std::chrono::seconds(10);
options.timeouts().body_idle = std::chrono::seconds(30);
options.budgets().set(srv::resource::connections, 512);
options.budgets().set(srv::resource::body_buffer_bytes, 8 * 1024 * 1024);
const auto valid = options.validate();
if (!valid.ok()) {
    std::cerr << valid.message() << '\n';
    return 1;
}
srv::native_server server(options);
```

### Defaults and startup

The default protocol set is HTTP/1.0 plus HTTP/1.1. The default loop mode is
`managed`; workers 0 lets the runtime select concurrency. At most 64 listeners
and 1024 explicit workers are accepted. Timeout fields must be positive and at
most 24 hours. Defaults are handshake 30 s, header 30 s, body idle 60 s,
suspension 300 s, write idle 30 s, WebSocket close 10 s and drain 30 s.

`validate()` is pure semantic validation: no descriptors, environment reads,
credential loading or binding. `resolve_limits(out)` also checks build support
and computes startup policy. `listen()` checks credentials/backend readiness,
validates, binds configured listeners and starts hosting. Check its outcome:
startup can still fail because an address is unavailable or credentials are
missing. Failed partial binding is terminal for that server object; construct a
new object for another attempt.

### Budgets and protocol capacities

`budgets()` holds aggregate resource capacities: connections, streams, header
bytes/fields, body buffers, response queues, HPACK/QPACK tables, blocked headers,
QUIC reassembly, WebSocket messages, timers and routes. The resource budget model
charges a reservation against its scope and ancestors. Byte capacities count
storage in flight; count capacities limit live objects. Response queue charges
include retained storage and metadata, so they can exceed payload byte counts.

Default server capacities include 1024 connections, 4096 streams, 1 MiB header
bytes, 256 header fields, 8 MiB body buffers, 4 MiB response queues and 1024
routes. Use the [budget header](src/httpserver/server/budgets.hpp) for all defaults
and ceilings. A body admission cap bounds that request's buffering and is
separate from aggregate server capacity.

The optional `http2()`, `http3()` and `quic()` profiles tune their respective
protocol state independently. They do not enable a protocol. For example,
after choosing an appropriate TLS TCP listener and enabling HTTP/2:

```cpp
options.http2().emplace().max_concurrent_requests = 192;
options.http2()->decoder_table_bytes = 8192;
options.http2()->encoder_table_bytes = 16384;
srv::effective_protocol_limits limits;
const auto resolved = options.resolve_limits(limits);
if (!resolved.ok()) {
    std::cerr << resolved.message() << '\n';
    return 1;
}
```

Profiles must fit aggregate budgets and protocol bounds. See the
[capacity tables](docs/v3-protocol-limits.md) and
[protocol options example](examples/protocol_options.cpp) for complete HTTP/2,
HTTP/3 and QUIC configurations. `native_server::effective_limits(out)` exposes
the immutable resolved local policy before listening and after stopping. It is
startup policy, not negotiated peer SETTINGS, live queue occupancy or a way to
resize a running engine. Failure leaves `out` unchanged.

[Back to TOC](#table-of-contents)

## Resource ownership

Routes own their callables; captured smart pointers express shared application
ownership. The server retains listener credential stores and copies options.
References returned by `hooks()` and `peer_policy()` are valid for the server's
lifetime. The `readiness()` adapter is borrowed and server-owned.

An `exchange` and its request head belong to the active request task. Read its
fields and captures there; copy strings or values that must survive the request.
Body read results view your destination buffer. Keep buffers alive across their
awaited operations and avoid moving an exchange while a read/write is pending.

`response_definition` shares immutable state. A definition must outlive every
`send_definition()` task using it; capturing the definition by value in a route
is a convenient way to ensure this. Its source determines replay and storage
ownership, described under [Response](#response). Handles such as a
`drain_ticket` or an accepted WebSocket session own their associated state, but
they do not make unrelated application captures live longer.

[Back to TOC](#table-of-contents)

## Routing

All enabled HTTP versions use the same route table. Register before `listen()`;
registration closes once `listen()` runs and later attempts return
`invalid_state`. Registration returns typed failures for malformed patterns,
invalid/empty methods or handlers, duplicate overlapping registrations and
route-budget exhaustion.

`route(method, pattern, handler)` accepts one known or extension method.
`route(method_set, pattern, handler)` accepts multiple known methods.
`route_prefix(method_set, pattern, handler)` matches a subtree. A method set is
built with `set(http::method_id::get)` and similar calls; an extension method is
created with `http::method::extension("CUSTOM")` and registered individually.
`route_sync` registers one bounded synchronous method at a time.

Patterns are slash-separated literal segments or whole `{name}` segments.
Parameter names contain letters, digits or underscores and must be unique
within a pattern. Captures are separate from query/form fields and available
in registration-pattern order through `exchange::path_args()`.

This helper registers a parameterized coroutine route and returns the
registration outcome (include `<httpserver.hpp>`):

```cpp
http::outcome add_item_route(srv::native_server& server) {
    return server.route(http::method::known(http::method_id::get), "/items/{id}",
        [](httpserver::exchange& x) -> httpserver::task<void> {
            const std::string text = x.path_args().front().value;
            http::fields fields;
            fields.append("Content-Length", std::to_string(text.size()));
            if (!x.start_response(http::status::from_code(200), fields).ok()) co_return;
            const auto written =
                co_await x.writer().write(std::as_bytes(std::span(text)));
            if (!written.status.ok()) co_return;
            if (!(co_await x.writer().finish()).status.ok()) co_return;
        });
}
```

### Precedence and methods

Full matches are considered before prefixes. Within full matches, the first
registration accepting the method wins; literals and parameter patterns share
that registration-order tier. Register `/items/special` before `/items/{id}` if
the literal should win. Among prefix matches, the deepest accepting pattern
wins, with registration order breaking ties. `/` is a catch-all prefix.

The tier decision is method-blind: a full-match path with the wrong method owns
the 405 instead of falling through to a prefix. The selected tier's methods
supply `Allow`. A complete path miss produces 404. GET does not imply HEAD;
register HEAD explicitly. The synchronous adapter keeps the corresponding body
length for HEAD without sending body bytes. Coroutine HEAD handlers must also
respect the protocol's body rules.

Use `options.not_found_response(factory)` and
`options.method_not_allowed_response(factory)` before constructing the server
to customize miss pages. A factory receives the request head and returns a
`hook_response`. A method-mismatch response receives an `Allow` field when one
was not supplied. A throwing factory or invalid returned status falls back to
the default page. See [routes.cpp](examples/routes.cpp) for these cases.

[Back to TOC](#table-of-contents)

## Request

### Head and peer data

`x.head()` returns the complete semantic head:

| Member/accessor | Meaning |
| --- | --- |
| `raw_target` | Exact received target bytes, preserving escapes and query |
| `route_path` | Validated/normalized input used by routing |
| `request_method` | `http::method`, including extension identity |
| `request_protocol` | `http::protocol` for this request |
| `head_fields` | Ordered header fields, including repetitions |
| `x.path_args()` | Named path captures; separate from query and form data |
| `x.peer()` | Immutable transport address/port snapshot |
| `x.connection_id()` | Library connection identity for correlation |
| `x.tls()` | Shared immutable TLS peer metadata; absent for plaintext |

`http::fields::first(name)` returns an optional first value;
`all(name)` returns all matching values with case-insensitive field names.
`entries()` preserves order. Use `append` to retain repeated values and
`replace` when replacing all values for one name.

Query decoding is explicit. Extract the suffix after `?` from `raw_target` and
pass its bytes to `forms::decode_urlencoded(encoded, limits, out)`. A
`form_fields` collection preserves repeated pairs and empty values. Cookie
parsing is also explicit: include `<httpserver/http/cookie.hpp>` and call
`http::parse_cookie_header()` on each Cookie field. Do not flatten repeated
headers or cookies unless that is the policy you want. The
[request data example](examples/request_data.cpp) demonstrates these operations.

### Body admission and incremental reads

A handler can answer directly from the head to refuse an upload before admitting
its body. `admit_body(body_policy{cap})` starts bounded body delivery and allows
`x.body()` operations. A zero policy cap selects the engine default; choose an
explicit positive bound for predictable request handling. A bodyless admitted
request reads immediately to EOF.

`read_some(buffer)` returns `body_read` with status, a view into the caller's
buffer, and `end_of_body`. Read until authoritative EOF before using trailers
or starting a response that depends on the whole upload. `collect(maximum)`
buffers the remaining body and fails `limit_exceeded` when it grows past the
cap; exactly-at-cap succeeds. Only one read or collect may be outstanding.
Consumption releases receive credit; stopping reads applies backpressure.

This complete handler function can be registered for POST with `server.route`.
It caps total upload bytes separately from buffer capacity, consumes EOF,
then returns a small streamed response:

```cpp
httpserver::task<void> upload_size(httpserver::exchange& x) {
    if (!x.admit_body({1024}).ok()) co_return;
    std::array<std::byte, 64> buffer;
    std::size_t total = 0;
    for (;;) {
        const auto read = co_await x.body().read_some(buffer);
        if (!read.status.ok()) {
            if (read.status.code() == http::outcome_code::limit_exceeded) {
                const auto rejected = x.respond(http::status::from_code(413), {});
                if (!rejected.ok()) co_return;
            }
            co_return;
        }
        total += read.data.size();
        if (total > 1024) {
            const auto rejected = x.respond(http::status::from_code(413), {});
            if (!rejected.ok()) co_return;
            co_return;
        }
        if (read.end_of_body) break;
    }
    // x.body().trailers() is final here; start_response closes the reader.
    const std::string text = "bytes=" + std::to_string(total) + "\n";
    http::fields fields;
    fields.append("Content-Length", std::to_string(text.size()));
    if (!x.start_response(http::status::from_code(200), fields).ok()) co_return;
    const auto written =
        co_await x.writer().write(std::as_bytes(std::span(text)));
    if (!written.status.ok()) co_return;
    if (!(co_await x.writer().finish()).status.ok()) co_return;
}
```

Include `<array>` alongside `<httpserver.hpp>`, `<span>` and `<string>`.
`x.body().trailers()` is separate from `head_fields` and final only at EOF.
A disconnect wakes parked body operations with a typed failure. Do not commit
response headers and then attempt to continue reading the upload.

### Forms and uploaded files

Form bodies are decoded through bounded adapters:

- `forms::read_urlencoded(x, limits)` admits and collects a URL-encoded body,
  returning fields or a typed rejection. `make_urlencoded_route(limits, handler)`
  adapts a value-returning form handler; `decode_urlencoded` works on bytes
  already in memory, including query strings.
- `forms::read_multipart(x, limits, sink)` incrementally parses multipart data
  into a `part_sink`. Limits bound body bytes, part count, per-part bytes and
  per-part header bytes.
- `make_multipart_route(limits, handler)` collects non-file fields in memory
  and drains file parts without retaining them. To retain file bytes in memory,
  implement an application `part_sink` under the same limits.
- `temp_file_part_sink` uses a caller-selected existing directory and owns
  cleanup of temporary files. `temp_file_options::random_names` avoids trusting
  client filenames;
  `should_keep` decides which completed files survive cleanup.

A URL-encoded handler can be written directly (include
`<httpserver/forms/urlencoded.hpp>`):

```cpp
httpserver::task<void> submit_form(httpserver::exchange& x) {
    const auto form = co_await httpserver::forms::read_urlencoded(x, {1024, 8});
    if (!form.ok()) {
        if (form.reject_status().valid()) {
            const auto rejected =
                x.respond(form.reject_status(), form.reject_fields());
            if (!rejected.ok()) co_return;
        }
        co_return;
    }
    // Validate form.fields before storing application data.
    http::fields fields;
    fields.append("Content-Length", "0");
    const auto accepted = x.respond(http::status::from_code(204), fields);
    if (!accepted.ok()) co_return;
}
```

The multipart sink must live throughout `read_multipart`; its destructor handles
cleanup on success, parser failure and cancellation. Keep policy applies only
to completed uploads. Treat a supplied filename as metadata, validate any name
used by the application, and copy fields/file metadata that must survive sink
destruction. See [forms.cpp](examples/forms.cpp) for memory-independent temp-file
handling and cleanup before the success response.

[Back to TOC](#table-of-contents)

## Response

An exchange takes one terminal decision. `respond(status, fields)` commits a
one-shot head with no active writer, suitable for empty replies/rejections.
`start_response(status, fields)` commits the streaming head and enables
`writer()`. Both close request-body reading. A failed precommit decision leaves
the exchange available; a second terminal action returns `invalid_state`.

### Streaming a body

After a successful `start_response`, await `writer().write(bytes)` and
`writer().finish(trailers)`. A successful write means the entire supplied span
was copied into the engine's bounded queue. It does not acknowledge peer receipt.
Keep the span's storage alive until the await completes. Only one write or
finish may be outstanding. Queue exhaustion suspends a writer until capacity,
failure or cancellation arrives.

This handler sends generated chunks without buffering the whole response:

```cpp
httpserver::task<void> download(httpserver::exchange& x) {
    http::fields fields;
    fields.append("Content-Type", "text/plain");
    if (!x.start_response(http::status::from_code(200), fields).ok()) co_return;
    const std::string chunk = "generated-chunk\n";
    for (int i = 0; i < 64; ++i) {
        const auto written = co_await x.writer().write(std::as_bytes(std::span(chunk)));
        if (!written.status.ok()) co_return;
    }
    http::fields trailers;
    trailers.append("X-End", "complete");
    if (!(co_await x.writer().finish(trailers)).status.ok()) co_return;
}
```

The engine chooses framing for an unframed streaming response according to the
protocol. If you declare `Content-Length`, supply exactly that body length.
Trailer support and body-forbidden statuses follow protocol rules; do not add
conflicting framing fields. Once the head is accepted, a later failure must
abort/reset/close according to the protocol rather than replace the response.

### Immutable response definitions

Create `response_definition` values through checked factories and send them with
`send_definition(exchange, definition, overlay)`. Copies share frozen state.
Every send owns its cursor, serialization, cancellation and overlay, so
replayable sources can be used concurrently without changing the definition.

| Factory/source | Library ownership | Replay contract |
| --- | --- | --- |
| `owned_bytes` | Body bytes | Reusable |
| `reopen_file` | Path; fresh file per send | Reusable if path resolves |
| `factory` | Fresh-producer callable | Reusable; concurrent-safe factory |
| `borrowed` | Span and `body_lease` | Reusable; immutable leased bytes |
| `owned_file` | Transferred `std::FILE*`/close | One send attempt |
| `owned_pipe` | Transferred `std::FILE*`/close | One send attempt |

This helper constructs an owned reusable response, captures it into a route and
checks each send. It can be called before listening:

```cpp
http::outcome add_cached_route(srv::native_server& server) {
    const std::string text = "cached\n";
    const auto bytes = std::as_bytes(std::span(text));
    httpserver::response_definition definition;
    const auto made = httpserver::response_definition::owned_bytes(
        http::status::from_code(200), {},
        std::vector<std::byte>(bytes.begin(), bytes.end()), definition);
    if (!made.ok()) return made;
    return server.route(http::method::known(http::method_id::get), "/cached",
        [definition](httpserver::exchange& x) -> httpserver::task<void> {
            httpserver::response_overlay overlay;
            overlay.headers.append("X-Source", "cache");
            const auto sent =
                co_await httpserver::send_definition(x, definition, overlay);
            if (!sent.status.ok()) co_return;
        });
}
```

An overlay appends headers after base fields and carries per-send trailers. It
never mutates the definition. Overlay/framing validation happens before head
commit; a conflict fails with the exchange untouched. Use an owned smart-pointer
keeper for a borrowed body's lease; an empty lease is rejected. A producer's
chunk data must remain valid until its next pull and must be nonempty unless it
marks the end.

Transferred file/pipe handles are closed exactly once on completion, failure,
cancellation or destruction of an unsent holder. One-shot claims are taken
before probing and are not refundable: a failed probe still spends the send
attempt, and concurrent losers fail `invalid_state`. Reopen a path or use a
fresh-producer factory for repeated sends. File reads/producers can occupy the
calling worker; choose sources appropriate to your latency requirements.
See [response_sources.cpp](examples/response_sources.cpp).

[Back to TOC](#table-of-contents)

## IP access control

Configure `options.peer_policy()` before construction or use
`server.peer_policy()` at runtime. The policy is enabled by default and starts
in `peer_policy_mode::accept_all`: unmatched peers are admitted. In
`reject_all`, unmatched peers are refused. In either mode a matching allow
entry takes precedence over a deny entry. Disabled policy admits all peers.

Runtime mutations are synchronized and callable from handlers. `deny(pattern)`,
`allow(pattern)`, `remove_denied(pattern)` and `remove_allowed(pattern)` return
outcomes. `set_mode()` changes the default stance and `set_enabled()` controls
policy evaluation. The following helper permits a private subnet and refuses
unmatched peers; check its result before relying on that policy:

```cpp
http::outcome restrict_peers(srv::native_server& server) {
    auto& policy = server.peer_policy();
    const auto allowed = policy.allow("192.168.0.0/16");
    if (!allowed.ok()) return allowed;
    policy.set_mode(srv::peer_policy_mode::reject_all);
    return http::outcome::okay();
}
```

Patterns accept exact IPv4/IPv6 addresses, CIDR and trailing IPv4 wildcards
such as `192.168.*.*`. IPv6 uses CIDR rather than wildcard components. Keep
wildcards trailing; mapped IPv6 CIDR is rejected. Peer metadata is the transport
peer, not an address asserted by an HTTP forwarding header. Admission decisions
are observable through `accept_decision`; live policy also participates in
request dispatch. See [operations.cpp](examples/operations.cpp).

[Back to TOC](#table-of-contents)

## Authentication and TLS

### Basic and Digest policies

Authentication policies operate on a request head. `basic_auth_policy::create`
accepts fixed credentials or an application validator; `check(head)` returns a
verdict with `allowed()`, a challenge status and challenge fields. A validator
may run concurrently and must keep its captures alive. Avoid recording decoded
credentials in logs or responses. Basic transmits a Base64-encoded password;
use TLS to protect authentication traffic.

For routine route protection, compose `make_basic_guard(policy, next)` or
`make_digest_guard(policy, next)` with a route. A guard sends its challenge when
a check fails, and invokes the next handler when accepted. Create a shared policy
wrapper for several protected routes and leave public routes unwrapped.

This helper obtains credentials from its caller and registers one guarded route:

```cpp
http::outcome add_private_route(srv::native_server& server,
                               std::string user, std::string password) {
    httpserver::auth::basic_auth_policy policy;
    const auto made = httpserver::auth::basic_auth_policy::create(
        "private", std::move(user), std::move(password), policy);
    if (!made.ok()) return made;
    return server.route(http::method::known(http::method_id::get), "/private",
        httpserver::auth::make_basic_guard(std::move(policy),
            [](httpserver::exchange& x) -> httpserver::task<void> {
                http::fields fields;
                fields.append("Content-Length", "0");
                const auto sent = x.respond(http::status::from_code(204), fields);
                if (!sent.ok()) co_return;
                co_return;
            }));
}
```

Digest supports `digest_algorithm::md5` and `digest_algorithm::sha_256`.
The default algorithm is MD5; explicitly choose SHA-256 in
`digest_auth_options` when required. The policy checks nonces, expiration and
replay state and produces an appropriate challenge, including stale handling.
Digest does not replace TLS protection. The [auth example](examples/auth.cpp)
selects SHA-256 explicitly and demonstrates both guard forms.

### Certificates, SNI and initial mTLS

A `tls_credentials` store publishes validated immutable generations. Configure
`tls_host_credentials` entries with canonical DNS host names, PEM certificate
chains and private keys, plus ALPN. Unknown or absent SNI uses `default_host`.
The listener retains the store, and each accepted TLS session pins a complete
selection through handshake and I/O retirement.

This helper configures a TLS TCP listener from application-supplied PEM strings.
It includes `<httpserver/server/tls_credentials.hpp>` via the umbrella:

```cpp
http::outcome configure_tls(srv::server_options& options,
                           std::string chain, std::string key) {
    auto store = std::make_shared<srv::tls_credentials>();
    srv::tls_credentials_config credentials;
    credentials.hosts.push_back({"a.example", std::move(chain), std::move(key),
                                 {"h2", "http/1.1"}});
    const auto published = store->replace(credentials);
    if (!published.ok()) return published;
    options.tls().provider = srv::tls_provider::system_default;
    options.tls().profile = srv::tls_profile::certificates;
    options.protocols().enable(http::protocol::http_2);
    options.add_listener({"127.0.0.1", 8443, true, store});
    return options.validate();
}
```

HTTP/2 needs enabled HTTP/2, a TLS TCP listener and an eligible certificate ALPN
selection containing `h2`. HTTP/3 needs enabled HTTP/3, a TLS UDP listener and
`h3`. Merely linking the provider enables neither listener. Use numeric bind
addresses; the credential's DNS name serves certificate/SNI selection.

For mutual TLS, choose `tls_profile::mutual_tls`, provide `trust_roots_pem`, and
set matching client-auth policy in the options and credential entry. Unset mode
means `require` for mutual TLS and `none` for certificate profiles. Explicit
`tls_client_certificate_mode::request` permits an anonymous peer but verifies a
presented chain; `require` needs a verified client chain. Authentication timing
is `initial_handshake`, including HTTP/3. `exchange::tls()` exposes an immutable
metadata snapshot: certificate presence/verification, subject/issuer DN, common
name, SHA-256 fingerprint and UTC validity times. Make application authorization
decisions from verified identity and your own policy.

### Credential rotation

Keep an owning store handle to rotate credentials. Call `store->replace(candidate)`
with a complete candidate configuration. Validation builds it synchronously and
publishes atomically; failure preserves the active generation. New selections
see the published generation, while existing sessions retain their selection.
Caller PEM buffers may be destroyed after `replace()` returns. Rotation does not
retrofit the identity of an established connection.

The [TLS rotation source](test/examples/tls_rotation.cpp) demonstrates replacement,
preservation on invalid input and selection across connections. The
[TLS profiles example](examples/tls_profiles.cpp) demonstrates certificate/SNI
and request/require client-auth modes.

### External PSK and ACME challenges

`tls_profile::external_psk` is available for eligible HTTP/1 TLS connections.
Configure a bounded `tls_psk_runtime`, a `tls_psk_config::lookup` and explicit
identity/key/attempt bounds. The lookup receives borrowed identity/context with
a deadline and cancellation token. Copy retained data and synchronize captured
state. TLS 1.3 identity is opaque bytes; TLS 1.2 uses its textual wire identity.

A timed-out callback that ignores cancellation retains its slot until returning;
its late result cannot authenticate. Server stop does not join arbitrary
application lookup callbacks. Keep their state alive, call the lookup runtime's
`request_stop()`, and check `wait_retired(deadline)` before releasing captures.
The runtime rejects waiting from its own callback. See
[tls_profiles.cpp](examples/tls_profiles.cpp) for a complete ownership pattern.

`publish_acme(challenge)` and `remove_acme(host)` manage application-supplied
TLS-ALPN challenge material. Publication validates certificate/key material,
the key-authorization digest and expiration. Challenge selection requires exact
canonical SNI, **sole** ALPN `acme-tls/1`, TCP and the authentic local port 443.
Ordinary HTTP ALPN remains separate. The application obtains challenge material,
coordinates issuance and removes it; the library does not issue certificates.

[Back to TOC](#table-of-contents)

## WebSocket support

Register a coroutine route and call `co_await x.upgrade(options)` before a
terminal response/body delivery. The same surface supports HTTP/1.1 Upgrade,
HTTP/2 Extended CONNECT and HTTP/3 Extended CONNECT on their configured
transports. TLS-off listeners serve HTTP/1.1 upgrades; H2/H3 require their TLS
listeners.

`ws_upgrade_options` specifies subprotocol preference, exact serialized origin
allowlists, absent-origin policy and session limits. By default an empty origin
allowlist accepts any syntactically valid origin and absent origins are allowed;
set explicit policy for browser-facing applications. Upgrade success means
bounded handshake admission (101 or a successful CONNECT response), not peer
acknowledgment. A refusal carries `rejection_status`/`rejection_fields` and leaves
the exchange open so the handler can send an ordinary HTTP response.

The accepted result owns a `websocket::session`:

- `receive()` yields owning message data or typed closure/failure. One receive
  may be outstanding at a time.
- `try_send(kind, bytes)` copies the whole payload when accepted. Temporary
  backpressure leaves queues unchanged and carries an OK status; inspect the
  disposition as well as the status.
- `writable()` waits for capacity for at least an empty frame. Retry `try_send`
  afterward; it does not promise room for an arbitrary message or acknowledge
  delivery. Only one writable wait may be outstanding.
- `close(code, reason)` begins a bounded close handshake. Register `on_close`
  once before terminal closure and retain the handle through notification.

Session entry points synchronize shared state, but moving/destroying a handle
must not race access to that handle. Server drain initiates Close 1001 and bounds
the handshake by its remaining deadline. Use [websocket_echo.cpp](examples/websocket_echo.cpp)
and its [session helper](examples/example_ws.hpp) for a complete receive/send/
backpressure loop and close lifetime management.

[Back to TOC](#table-of-contents)

## Server introspection and external event loops

`get_bound_port(listener_index)` reports the actual bound port, including an
OS-chosen ephemeral port. It returns 0 before listening or for an invalid index.
`is_running()` remains true during graceful rundown until stop initiation.
`effective_limits(out)` exposes resolved startup policy, and the diagnostic
channel supplies approximate live counters.

Set `options.loop() = srv::loop_mode::external` before construction to let an
application event loop own readiness and timer advancement. Workers remain
library-owned. `server.readiness()` returns a stable borrowed adapter in external
mode and `nullptr` in managed mode; before listen/after stop it is inactive.

The host loop follows this sequence:

1. Obtain `driver->interests()`: a coherent owned snapshot of socket interests,
   an optional wake interest and an optional next deadline.
2. Register the complete snapshot in the host loop. Omitted interests must be
   unregistered. Handles are borrowed; only the library closes them. Preserve
   the full-width Winsock carrier on Windows.
3. Wait for readiness or the deadline. Forward readiness with the original
   `(socket_key, generation)`, including wake events; dispatch an empty event
   batch when the deadline expires without socket readiness.
4. Call `driver->dispatch(events, steady_clock::now())`, check its outcome and
   replace registrations from a fresh snapshot. Dispatch never waits; overlapping
   or recursive calls on the same driver return `invalid_state`. Stale generation
   callbacks are ignored.
5. Remove host registrations and discard borrowed handles before destroying the
   server. A snapshot copy does not extend handle lifetime.

The complete [external loop example](examples/external_event_loop.cpp) uses
[host_poll.hpp](examples/host_poll.hpp) for POSIX poll and Windows WSAPoll,
including wake handles and deadline-derived waits. The
[external loop contract](docs/external-loop-contract.md) specifies lifecycle and
threading details for other host loops.

[Back to TOC](#table-of-contents)

## HTTP utilities

Use the semantic value headers rather than transport-specific constants:

- `http::method::known(method_id)` constructs a standard method;
  `method::extension(token)` preserves a valid extension token. `method_set`
  represents standard methods for multi-method registration.
- `http::status::from_code(code)` constructs a status and `valid()` checks it.
  Category predicates such as `success()` and `client_error()` classify it.
- `http::fields` preserves order/repetition, with `append`, `replace`, `first`,
  `all`, `count` and `entries` for access.
- `<httpserver/http/cookie.hpp>` provides `cookie`, `parse_cookie_header` and
  checked `render_set_cookie(cookie, out)`. Render each Set-Cookie separately;
  configure path, Secure, HttpOnly and SameSite according to application policy.
- `query_features()` reports build/provider support. See [Feature
  availability](#feature-availability) for how it relates to actual listeners.

[Back to TOC](#table-of-contents)

## Lifecycle hooks

Use `server.hooks().add<hook_phase::phase>(callback)` for server-wide request
policy/observation. Each callback receives its phase-specific context and
returns `hook_action`. The returned move-only `hook_handle` removes the
registration on destruction; keep it alive or call `detach()` for server-lifetime
registration. Add/remove operations are safe before and after listening.

There are eight phases:

| Phase | When | Capability |
| --- | --- | --- |
| `accept_decision` | Peer-policy verdict fixed | Observe verdict |
| `request_received` | Head before routing/body | Short-circuit |
| `route_resolved` | Lookup finished, including miss | Observe route |
| `before_handler` | Before call; also method mismatch | Short-circuit |
| `handler_exception` | Handler/eligible hook failure | Recovery response |
| `after_handler` | Head commitment | Mutate status/fields |
| `response_sent` | Engine accepted the committed head | Observe acceptance |
| `request_completed` | Exchange settles once | Observe typed result |

`hook_action::pass()` continues the chain. The first `respond_with(hook_response)`
in a short-circuit-capable phase stops the remaining callbacks in that phase.
Observation phases ignore response actions; `after_handler` offers head
mutation rather than whole-body replacement. Registration order is preserved,
and changes during firing affect subsequent snapshots.

For example, before listening, keep this handle in the controlling scope:

```cpp
auto head_hook = server.hooks().add<srv::hook_phase::after_handler>(
    [](srv::after_handler_ctx& context) {
        context.fields.append("X-Service", "example");
        return srv::hook_action::pass();
    });
```

`response_sent` means head acceptance by the engine, not wire delivery of the
head or body. If the engine rejects framing/field bytes, that phase does not
fire and `request_completed` reports failure. Observe per-body progress in the
handler's read/write loop. Use wrapping route handlers for route-specific
policy, including authentication.

Hook exceptions are contained and treated as pass. Unanswered precommit failures
from `request_received`, `route_resolved` or `before_handler` can be surfaced to
`handler_exception`; that chain is consulted at most once per request. Throws
from commit/completion phases cannot replace an accepted head. Keep callbacks
short, thread-safe and nonblocking; `accept_decision` runs on the accept task.
See the [hook contract](src/httpserver/server/hooks.hpp) and
[operations example](examples/operations.cpp).

### Bounded diagnostics

`server::diagnostics` is a copyable shared channel configured through
`options.diagnostic_channel()`. Default construction disables it. Records contain
scalar protocol/resource/lifecycle information rather than request/TLS identity.
Producers enqueue into a bounded 128-record queue; counters track drops and
callback failures.

Call `channel.dispatch(max_events)` from your chosen control thread to deliver
a bounded batch outside engine/queue locks. New events cannot extend that call
beyond the queue observed at entry. Concurrent/recursive dispatch returns
`invalid_state`, and callback exceptions are counted/contained. Keep callback
captures alive through the last dispatch; destruction never dispatches.
`counters()` exposes event totals, active connections/streams and charged response
queue bytes. Fields are individually atomic; concurrent snapshots are approximate.

[Back to TOC](#table-of-contents)

## Threading contract

The native [architecture](specs/architecture/v3/architecture.md) and public
contracts distinguish executor-owned work from synchronized handles:

1. Server methods are thread-safe unless documented otherwise; lifecycle calls
   (`listen()`/`stop()`) belong to one controlling thread. Routes close at listen,
   while hook and peer-policy mutation remain runtime-safe.
2. Different requests may invoke the same callable/captured state concurrently.
   Synchronize application state. An exchange's decisions, reader and writer
   run on its handler executor. Copy request data needed outside that lifetime.
3. `request_stop()` is nonblocking, idempotent and handler-safe. It releases
   listeners and disconnects active exchanges. `stop()` additionally closes
   engines and joins workers; call it from a controlling thread outside counted
   work. The destructor performs stop. Keep application captures alive until
   this join completes.
4. `begin_drain(budget, ticket)` stops accepting new work and initiates graceful
   rundown without waiting. For HTTP/1, in-flight work flushes, idle connections
   close and unstarted pipelined heads are dropped; WebSockets begin Close 1001.
   The positive budget bounds the ticket's wait. A server supports one drain;
   calls before listen, after stop or after a previous drain return invalid_state.
5. `ticket.wait(result)` reports `completed` or `deadline_expired` with the
   remaining count at expiry. Waiting from work counted by that drain returns
   `would_deadlock`. Tickets retain their counted scope after server destruction.
   Stop during a drain can complete it before expiry; an expired result remains
   expired after cancellation unwinds.

### Suspension and cancellation

`x.suspend(resume_signal&)` obtains a signal for a pending decision. Copy the
owning signal to application work and `co_await signal.wait()` or
`signal.wait_for(duration)` on the handler executor. Another thread calls
`signal.signal()`; the result distinguishes resumed, timeout and cancellation.
Exchange disconnect wakes pending operations. `x.cancellation()` gives a fan-out
stop token whose `cancelled()` awaitable throws `cancelled_exception`; ordinary
body/writer operations report typed cancellation. Use RAII to retire pending
application registrations on every path.

Shutdown can cancel pending work but cannot make arbitrary blocking application
code cooperative. Respect cancellation/deadlines in application operations and
retain their captures until retirement. The [lifecycle example](examples/lifecycle.cpp)
shows resume, timeout, cancellation and drain without leaking an `exchange&` to
another thread. External PSK callbacks have their own explicit retirement
contract described under [Authentication and TLS](#authentication-and-tls).

[Back to TOC](#table-of-contents)

## Error propagation

Check `outcome.ok()` before consuming a successful operation's outputs.
`outcome.code()` is the stable semantic discriminator and `message()` is a
human-readable diagnostic. Reader/writer/send/upgrade results embed an outcome
alongside their operation-specific data.

| Code | Typical cause |
| --- | --- |
| `invalid_argument` | Invalid configuration, pattern, status or field input |
| `invalid_state` | Late registration, duplicate decision, spent source |
| `limit_exceeded` | Body, queue, route or protocol capacity exhausted |
| `protocol_error` | Invalid peer framing/protocol behavior |
| `not_supported` | Feature unavailable in this build or configuration |
| `connection_closed`, `cancelled`, `timeout` | Closure/cancellation/deadline |
| `would_deadlock` | Waiting from work counted by the wait |
| `peer_refused` | Peer access policy rejected admission |

A route that throws before commitment is contained. The `handler_exception`
chain may supply a recovery response; otherwise the dispatcher synthesizes a
bare 500 with no fixed text body. Returning without a terminal decision also
synthesizes 500. A handler exception after commitment aborts the response through
the protocol engine; it cannot replace the accepted head. Disconnect cancellation
is a quiet end, reported through completion state rather than a new error page.

To answer 400/404/503 intentionally, call `respond`, stream a response or return
a synchronous value with that status. Exceptions do not map automatically to
HTTP status codes. Keep exception details in application diagnostics and avoid
echoing internal data to clients. General C++ allocation failures can still
throw where specified (for example, readiness snapshot allocation); typed
outcomes do not promise that every allocation is nonthrowing. See the
[request lifecycle owner](src/detail/request_lifecycle.cpp) for containment.

[Back to TOC](#table-of-contents)

## Feature availability

`query_features()` returns build support flags `tls_provider`, `tcp_tls`,
`http2`, `http3` and a process-lifetime `provider_version` string. Provider
availability and transport support are distinct from configured listeners:

| Capability | Public listener requirements |
| --- | --- |
| HTTP/1 plaintext | TCP; default protocols; either loop mode |
| HTTP/1 TLS | Provider, TLS profile/store and TLS TCP |
| HTTP/2 | Build support, enabled H2, certificate TLS TCP, ALPN `h2` |
| HTTP/3 | Build support, enabled H3, certificate TLS UDP, ALPN `h3` |
| WebSocket | HTTP/1.1 Upgrade or H2/H3 Extended CONNECT |
| Basic/Digest, forms, routes, hooks | Native; no feature switches |

A TLS-off build keeps the same public declarations. `query_features()` returns
false support flags and an empty provider version; encrypted operations return
`not_supported` before binding. `validate()` checks semantics independently of
build availability, so also check `resolve_limits()`/`listen()` and credential
publication. A TLS-enabled build with no TLS listener remains a plaintext
server.

Lower-level HTTP/2 and QUIC/HTTP/3 engines are not additional listener modes.
In particular, engine support for cleartext HTTP/2 machinery does not provide
a public h2c listener. Initial mTLS is supported; post-handshake authentication
is not an available configuration. External PSK is restricted to eligible
HTTP/1 TLS connections. ACME publication selects application-supplied challenges
under the strict TCP/SNI/ALPN/port rules described above.

[Back to TOC](#table-of-contents)

## Examples

Configure with `--enable-examples`, then run `make -C examples` in the build
directory. These are the fifteen native built targets; each source states its
arguments. Programs that print `PORT=...` use OS-chosen ports so multiple demos
can run without fixed-port collisions.

| Target | Source | Demonstrates |
| --- | --- | --- |
| `external_event_loop` | [external_event_loop.cpp](examples/external_event_loop.cpp) | Host poll/WSAPoll, wake interests and deadlines |
| `routes` | [routes.cpp](examples/routes.cpp) | Full/prefix routing, captures, methods, HEAD and miss pages |
| `protocol_options` | [protocol_options.cpp](examples/protocol_options.cpp) | Validation, typed capacities and effective policy |
| `websocket_echo` | [websocket_echo.cpp](examples/websocket_echo.cpp) | Upgrade, bounded messages, backpressure and closure |
| `http3` | [http3.cpp](examples/http3.cpp) | Managed/external QUIC HTTP/3 and WebSocket routes |
| `request_data` | [request_data.cpp](examples/request_data.cpp) | Repeated fields, query decoding and cookies |
| `body_streaming` | [body_streaming.cpp](examples/body_streaming.cpp) | Early refusal, bounded upload and generated download |
| `auth` | [auth.cpp](examples/auth.cpp) | Basic and SHA-256 Digest guards |
| `forms` | [forms.cpp](examples/forms.cpp) | URL-encoded forms, temp files and keep/cleanup |
| `response_sources` | [response_sources.cpp](examples/response_sources.cpp) | Owned, leased, reopened, factory and one-shot sources |
| `operations` | [operations.cpp](examples/operations.cpp) | Peer policy, eight hooks, diagnostics and resource limits |
| `lifecycle` | [lifecycle.cpp](examples/lifecycle.cpp) | Suspension, cancellation, timeout and drain tickets |
| `tls_profiles` | [tls_profiles.cpp](examples/tls_profiles.cpp) | SNI, initial mTLS, external PSK and ACME publication |
| `http2` | [http2.cpp](examples/http2.cpp) | Managed/external TLS HTTP/2 and WebSocket routes |
| `tls_rotation` | [tls_rotation.cpp](test/examples/tls_rotation.cpp) | Atomic credential replacement and pinned sessions |

For example, from the build directory:

```sh
./examples/routes 30
./examples/body_streaming 30
./examples/forms /tmp 30
./examples/external_event_loop 30
```

Run these individually, read the printed port and use curl in a second terminal.
The numeric argument bounds the demo's runtime. `operations` and `lifecycle`
accept stdin commands instead; TLS demos need caller-supplied material. See the
[native example commands](examples/README.md#native-v3-examples) for complete
invocations and clients. TLS-specific demos report unavailability on a TLS-off
build.

[Back to TOC](#table-of-contents)

## Community

Please read the [Code of Conduct](CODE_OF_CONDUCT.md) and
[contribution guide](CONTRIBUTING.md). Report issues and contribute patches through
[the project repository](https://github.com/etr/libhttpserver).

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

Thanks to the many contributors to libhttpserver, everyone who has filed issues,
sent patches and stress-tested the library, and the GNU libmicrohttpd developers
whose work supported its early development. See the project's Git history for
the complete contributor list.

If libhttpserver is useful to you, consider sponsoring continued
maintenance: <https://github.com/sponsors/etr>.

[Back to TOC](#table-of-contents)
