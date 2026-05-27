libhttpserver Examples
======================

Every example is a standalone program built from a single `.cpp`. To
build the suite locally:

    ./bootstrap && mkdir build && cd build && ../configure && make

Each binary lands in `build/examples/`. To verify that an example also
compiles against an *installed* libhttpserver (the consumer view), run
`scripts/verify-installed-examples.sh` after `make install`. To enforce
the structural invariants on `hello_world.cpp` and `shared_state.cpp`
(LOC budget, lambda-only form, mutex usage), run
`scripts/check-examples.sh`.

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

* `banned_ip_log.cpp` — a single `accept_decision` hook logs every
  banned-IP rejection to stderr. Resolves issue #332.
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
* `minimal_ip_ban.cpp` — `block_ip` / `unblock_ip` under the default
  ACCEPT policy.
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
