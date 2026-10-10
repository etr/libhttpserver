libhttpserver Examples
======================

Native v3 examples
------------------

Read the [migration guide](../docs/v2-to-v3-migration.md) and
[native API page](../docs/v3-api.md). Public examples include only installed
native headers; the shared route/host helpers live beside their source files.

| Example | What it demonstrates |
| --- | --- |
| [v3_routes.cpp](v3_routes.cpp) | Bounded `route_sync` GET/HEAD, coroutine POST echo, handler-safe stop |
| [v3_protocol_options.cpp](v3_protocol_options.cpp) | HTTP/1.1, H2, H3 and TLS options; semantic validation versus typed public listener rejection |
| [v3_websocket_echo.cpp](v3_websocket_echo.cpp) | HTTP/1.1 upgrade/rejection, text/binary echo, `try_send` backpressure, `writable`, retained session through close |
| [v3_external_event_loop.cpp](v3_external_event_loop.cpp) | External host readiness, generation tokens, worker wakeups and deadlines |

From the repository root (native C++20 build; no MHD or GnuTLS prerequisites):

```sh
./bootstrap
mkdir -p build/native
cd build/native
CXX=clang++ CXXFLAGS='-std=c++20 -O0 -g' ../../configure --disable-v3-tls --enable-examples
make -j2
./examples/v3_routes 30
# Another terminal: substitute the printed PORT value.
curl http://127.0.0.1:PORT/hello
curl --data-binary 'hello' http://127.0.0.1:PORT/echo
curl -X POST http://127.0.0.1:PORT/stop
```

`v3_routes` and `v3_websocket_echo` listen on ephemeral loopback ports, print
`PORT=...`, and default to a ten-second lifetime; an optional integer selects
1–3600 seconds. `/stop` cancels active exchanges and need not return a reply.
Run `./examples/v3_protocol_options` for the configuration report and
`./examples/v3_external_event_loop 10` for a bounded external host.
The WebSocket example accepts `/echo` without an Origin policy for local smoke
use; applications should configure allowed origins, subprotocols and limits.

The protocol example compiles in TLS-off and TLS-on builds. It validates each
configuration and reports `not_supported` with port zero for public TLS/H2/H3
listeners. It does not serve H2/H3. Shared-handler tests exercise their semantic
contract; the [TASK-179 audit](../docs/task-179-behavior-parity.md) separately
records private encrypted transport and Extended CONNECT evidence.

The native examples also compile as installed consumers. With libhttpserver.pc and prerequisite .pc metadata
available via PKG_CONFIG_PATH, run from the repository root:

```sh
clang++ -std=c++20 -I. examples/v3_routes.cpp -o /tmp/v3_routes $(pkg-config --cflags --libs libhttpserver)
clang++ -std=c++20 examples/v3_websocket_echo.cpp -o /tmp/v3_websocket_echo $(pkg-config --cflags --libs libhttpserver)
clang++ -std=c++20 examples/v3_protocol_options.cpp -o /tmp/v3_protocol_options $(pkg-config --cflags --libs libhttpserver)
clang++ -std=c++20 examples/v3_external_event_loop.cpp -o /tmp/v3_external_event_loop $(pkg-config --cflags --libs libhttpserver)
```

Private TLS rotation demonstration
----------------------------------

[test/examples/v3_tls_rotation.cpp](../test/examples/v3_tls_rotation.cpp) is
repository-internal, requires `HTTPSERVER_COMPILATION`, and cannot use installed
public headers. Build with the genuine provider described in
[native-tls-build.md](../docs/native-tls-build.md): configure a fresh build with
`--enable-v3-tls --enable-examples` and explicit `V3_TLS_CFLAGS`/`V3_TLS_LIBS`, run
`make -j2`, then `make -C test v3_tls_rotation`. Run `./test/v3_tls_rotation` from
the build root. It loads committed fixtures and demonstrates retained old
selection, new active selection, and failed replacement preserving that selection.
`make -C test check TESTS='tls_credentials tls_credentials_rotation v3_tls_rotation'
check_PROGRAMS='tls_credentials tls_credentials_rotation v3_tls_rotation'` runs
the private demonstration and independent established-session handshake tests.
There is no public native credential installation/rotation API.

Legacy v2 examples
------------------

The remaining examples use the legacy umbrella and MHD-backed APIs. Each is a
standalone program. They build with the same `--enable-examples` configuration;
native sources link v3core separately and do not require legacy `HAVE_WEBSOCKET`.
`scripts/verify-installed-examples.sh` compiles examples after installation.
`scripts/check-examples.sh` enforces the pinned v2 introductory examples.

Start here
----------

* `hello_world.cpp` — ten lines, no subclassing, no raw pointers. The
  canonical "hello world" demo and the PRD §3.4 acceptance fixture for
  the v2.0 lambda idiom.
* `shared_state.cpp` — the canonical example of when the class form is
  the right shape: an `http_resource` subclass that shares a `std::mutex`-
  guarded counter between `render_get` and `render_post`.

HTTP basics
-----------

* `setting_headers.cpp` — attach response headers fluently with
  `with_header`.
* `hello_with_get_arg.cpp` — read a query-string parameter from
  `http_request::get_arg`.
* `args_processing.cpp` — iterate every form/query argument.
* `custom_error.cpp` — install custom `not_found_handler` and
  `method_not_allowed_handler` and let `set_allowing` restrict methods
  on a resource.
* `allowing_disallowing_methods.cpp` — narrow a resource to a single
  HTTP method via the allow mask.
* `url_registration.cpp` — exact paths, prefix matches, regex paths,
  and parametric segments (with optional per-segment regex).
* `handlers.cpp` — register distinct lambdas for GET and POST on the
  same path; the dispatcher composes them.

Response shapes
---------------

* `minimal_file_response.cpp` — stream a file from disk.
* `binary_buffer_response.cpp` — return a binary buffer (e.g. a PNG).
* `iovec_response_example.cpp` — gather a response body from multiple
  borrowed buffers without copying.
* `pipe_response_example.cpp` — stream from a pipe filled by a
  background thread.
* `empty_response_example.cpp` — bodyless responses for DELETE / HEAD.
* `minimal_deferred.cpp` — deferred response: the body is produced
  asynchronously by a callback.
* `deferred_with_accumulator.cpp` — deferred response that mutates
  shared state across calls.

TLS and authentication
----------------------

* `minimal_https.cpp` — enable TLS with PEM key/cert files.
* `minimal_https_psk.cpp` — TLS with pre-shared keys.
* `basic_authentication.cpp` — per-request HTTP Basic auth inside the
  handler.
* `centralized_authentication.cpp` — server-wide `auth_handler` and
  `auth_skip_paths`.
* `digest_authentication.cpp` — per-request HTTP Digest auth via
  `http_request::check_digest_auth`.
* `client_cert_auth.cpp` — mutual TLS with X.509 client certificates.
  Ships as a documentation artifact; not wired into `noinst_PROGRAMS`.

Lifecycle hooks
---------------

The hook bus (DR-012, §4.10) lets user code observe and short-circuit
every interesting point in the request lifecycle. The four examples
below demonstrate the user-visible resolution of issues #332, #273,
#281, #69, and the per-route hook contract introduced in TASK-051.

* `denied_ip_log.cpp` — a single `accept_decision` hook logs every
  denied-IP rejection to stderr. Resolves issue #332.
* `early_413.cpp` — a single `request_received` hook returns
  `respond_with(http_response::empty().with_status(413))` when
  `Content-Length` exceeds the configured cap. The request body never
  crosses the I/O boundary. Resolves issue #273.
* `clf_access_log.cpp` — a single `response_sent` hook formats a
  Common Log Format line using the structured context fields
  (`status`, `bytes_queued`, `elapsed`). Resolves issues #281 and #69.
* `per_route_auth.cpp` — `http_resource::add_hook(before_handler, ...)`
  guards a single route with HTTP Basic auth without touching sibling
  routes. Demonstrates per-route hook scoping (DR-012).

Operations
----------

* `daemon_info.cpp` — introspect the running daemon (bound port,
  listen FD, active connections).
* `external_event_loop.cpp` — drive `EXTERNAL_SELECT` from the
  application's loop via `run_wait`.
* `custom_access_log.cpp` — server-wide access-log callback.
* `minimal_ip_access_control.cpp` — `deny_ip` / `allow_ip` under the
  `ACCEPT` and `REJECT` policies.
* `turbo_mode.cpp` — turbo, suppressed Date header, fastopen queue,
  listen backlog.
* `service.cpp` — the kitchen-sink reference example: CLI args,
  optional TLS, all `render_*` overrides.

Benchmarks
----------

* `benchmark_select.cpp`, `benchmark_threads.cpp`,
  `benchmark_nodelay.cpp` — micro-benchmarks. See `test/v1_baseline/` for
  v1.x reference numbers.

WebSockets
----------

* `websocket_echo.cpp` — `websocket_handler` subclass registered via
  `register_ws_resource` with `std::make_unique`.

File upload
-----------

* `file_upload.cpp`, `file_upload_with_callback.cpp` — multipart
  upload, GET serves the HTML form and POST processes the parts.

Creating Certificates
=====================

Self-signed certificates can be created using OpenSSL using the
following steps:

    $ openssl genrsa -des3 -passout pass:x -out server.pass.key 2048
    $ openssl rsa -passin pass:x -in server.pass.key -out server.key
    $ openssl req -new -key server.key -out server.csr
    $ openssl x509 -req -days 365 -in server.csr -signkey server.key -out server.crt

On the last step when prompted for a challenge password it can be left
empty.

Thanks to https://devcenter.heroku.com/articles/ssl-certificate-self
for these instructions.
