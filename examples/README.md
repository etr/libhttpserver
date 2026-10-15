# libhttpserver examples

## Native v3 examples

All fifteen programs below use installed public C++20 APIs. The native build
requires a C++20 compiler, Autotools, and pthreads; it does not require MHD or
GnuTLS. From the repository root:

```sh
./bootstrap
mkdir -p build/native
cd build/native
CC=clang CXX=clang++ CXXFLAGS='-std=c++20 -pthread -O1 -g' \
  ../../configure --disable-v3-tls --enable-examples
make -C src -j2
make -C examples -j2
```

For TLS, configure another build with `--enable-v3-tls` and the OpenSSL 3.5 LTS
provider described in [native TLS setup](../docs/native-tls-build.md).
`V3_TLS_CFLAGS` and `V3_TLS_LIBS` select that provider. Examples never include
provider headers. TLS-off builds include every program: TLS-specific examples
exercise credential/runtime/publication/listener calls, report their typed
`not_supported` outcomes and port zero, and exit successfully.

| Program/source | Public API families |
| --- | --- |
| [routes](routes.cpp) | Sync/coroutine routes, captures, methods, full/prefix precedence, custom errors, exception boundaries |
| [request_data](request_data.cpp) | Request head/raw target, ordered fields, cookies, bounded query decoding |
| [body_streaming](body_streaming.cpp) | Header-time refusal/admission, incremental reads, upload/download trailers, bounded writes |
| [auth](auth.cpp) | Basic/Digest policy creation and composed route guards |
| [forms](forms.cpp) | URL-encoded adapter/reader, multipart sink, file cleanup/keep policy |
| [response_sources](response_sources.cpp) | All six body sources, leases, overlays, send reports, one-shot claims, ICY status flag |
| [operations](operations.cpp) | Eight lifecycle hooks, diagnostics, budgets/timeouts, live transport peer policy |
| [lifecycle](lifecycle.cpp) | Application suspension/resume, timeout, cancellation/RAII, handler stop, graceful drain |
| [protocol_options](protocol_options.cpp) | Protocol selection, feature reporting, validation, typed unavailable listeners |
| [external_event_loop](external_event_loop.cpp) | Readiness snapshots, generations, wakeups, deadlines and host ownership |
| [websocket_echo](websocket_echo.cpp) | H1 upgrade/refusal, text/binary echo, backpressure, owning session lifetime |
| [http2](http2.cpp) | Certificate TCP H1/H2, RFC8441 Extended CONNECT WebSocket |
| [http3](http3.cpp) | Shared TCP H1/H2 and UDP H3 routes/trailers, RFC9220 WebSocket, both loop modes |
| [tls_profiles](tls_profiles.cpp) | SNI/default certificates, initial mTLS, external PSK runtime, ACME publication/removal |
| [tls_rotation](../test/examples/tls_rotation.cpp) | Live valid/rejected replacement; established/new connection generations |

Run the following commands from the configured build directory. For servers
printing `PORT=12345`, set `PORT=12345` in the client terminal. Routes, request
data, bodies, auth and H1 WebSocket hosts accept `[seconds]`
(default 10, range 1–3600) and stop automatically. Forms and sources take their
required input first, then `[seconds]`. Operational, lifecycle, TLS-profile and
rotation hosts accept the documented stdin commands; EOF also stops them.
H2/H3 print a numeric first port line. Every listener binds loopback.

### Routes

```sh
./examples/routes 30
# In another terminal with PORT set:
curl -i "http://127.0.0.1:$PORT/items/42"
curl -i "http://127.0.0.1:$PORT/items/special"
curl -i "http://127.0.0.1:$PORT/tree/deep/child"
curl -i -X CUSTOM "http://127.0.0.1:$PORT/extension"
curl -i -X DELETE "http://127.0.0.1:$PORT/methods"
curl --data-binary hello "http://127.0.0.1:$PORT/echo"
curl -X POST "http://127.0.0.1:$PORT/stop"
```

Observe `42`, `special`, `deep`, `custom`, then a custom 405 with merged
`Allow: GET, POST`. `/hello` explicitly registers both GET and HEAD; HEAD is
never automatically registered. Full matches run before prefixes; full-tier
siblings use registration order, and prefixes choose greatest depth then
registration order. Register literals before captures. Custom methods are
normalized to uppercase (`custom` also works); v2 regex routes are absent.
`/throw` produces a sanitized 500. `/throw-committed` closes the exchange without
a replacement response. `/stop` initiates cancellation; its caller has no
promised reply.

### Request values and cookies

```sh
./examples/request_data 30
curl -i -H 'X-Repeat: first' -H 'X-Repeat: second' \
  -H 'Cookie: a=1; a=2' -H 'Cookie: b=3' \
  "http://127.0.0.1:$PORT/inspect?a=one&a=two+words&encoded=%2F"
```

Observe the unchanged encoded target, separate route path, ordered repeated
fields/cookies/query pairs and two separate Set-Cookie fields. Only the query
substring is decoded, under 256 bytes/8 fields. Malformed `%` gives 400; excess
query bytes/fields give 413. Startup also checks field append/replacement/first/
all/entry order and invalid cookie rendering preserving its output string.

### Incremental bodies

```sh
./examples/body_streaming 30
curl -i -H 'Expect: 100-continue' --data-binary hello "http://127.0.0.1:$PORT/reject"
curl -i --data-binary hello "http://127.0.0.1:$PORT/upload"
curl -i --raw "http://127.0.0.1:$PORT/download"
```

Observe refusal before body admission, `bytes=5`, and 64 bounded generated
chunks followed by ordered `X-End` trailers. `/download/slow` streams a fixed
4 MiB payload from one 32 KiB buffer, with startup/finish-accepted handshakes
for a slow reader smoke. Its explicit 16 MiB output budget includes retained
allocation and bounded growth scratch; finish acceptance is not peer receipt. Upload uses a fixed 64-byte buffer
and explicitly enforces a 1024-byte total cap; admission's buffering policy is
not a total streaming byte limit. Completed request trailers are inspected
only after EOF. `start_response()` closes the body reader: consume uploads
before responding; this API does not demonstrate simultaneous duplex bodies.
Writer failures retire the coroutine without attempting another response.

### Authentication

```sh
./examples/auth 30
curl -i "http://127.0.0.1:$PORT/auth/basic"
curl -i -u demo:secret "http://127.0.0.1:$PORT/auth/basic"
curl -i --digest -u demo:secret "http://127.0.0.1:$PORT/auth/digest"
curl -i "http://127.0.0.1:$PORT/public"
```

Missing/wrong credentials receive 401 challenges; correct demonstration
credentials receive `authenticated`. Digest uses SHA-256 and the library's
nonce/replay ledger. `/public` remains accessible. Basic requires TLS for
credential confidentiality; plaintext loopback here is a local demonstration.

### Forms and uploads

```sh
UPLOADS=$(mktemp -d)
./examples/forms "$UPLOADS" 30
curl -i --data 'a=one&a=two+words' "http://127.0.0.1:$PORT/form"
curl -i --data 'a=one&a=two+words' "http://127.0.0.1:$PORT/form/read"
printf abc > /tmp/safe.txt
curl -i -F 'note=one' -F 'note=two' -F 'file=@/tmp/safe.txt' \
  "http://127.0.0.1:$PORT/multipart"
```

Both URL-encoded shapes preserve repeats (1024 bytes/8 fields); malformed
encoding gives 400 and excess bytes give 413. Multipart explicitly limits
4096 body bytes, 8 parts, 1024 bytes/part, 512 header bytes/part. Responses
show ordered text fields and sanitized filename/size without absolute paths.
The sink and exchange remain alive through the awaited reader. Partial files
are removed on abort; completed files are removed before replying by default.
`/multipart/keep` deliberately retains a completed `safe.txt`; the application
then owns/removes it. Files use random storage names independent of their
client filenames, so later uploads with the same client name leave retained
files intact. Use a private application-supplied upload directory.

### Response sources and ownership

```sh
printf 'reopened\n' > /tmp/v3-body.txt
./examples/response_sources /tmp/v3-body.txt 30
for kind in bytes borrowed reopen factory file pipe missing closes; do
  curl -i "http://127.0.0.1:$PORT/$kind"
done
curl -i "http://127.0.0.1:$PORT/file"
curl -i "http://127.0.0.1:$PORT/pipe"
```

Observe independent reusable bodies, per-send `X-Send` overlays, factory/pipe
trailers, an owned file starting at its transferred position, and a prefilled
pipe ending at EOF. Later file/pipe attempts become intentional precommit 409;
`/closes` shows exactly-once release. `/missing` deliberately maps checked
precommit failure to 404. Invalid empty leases are checked at startup.

A lease owns the **actual immutable span storage**. Definition and exchange
outlive `send_definition`; each concurrent factory call creates a fresh mutable
producer and its spans survive until its next pull. Handle claims are
nonrefundable, including failed preparation. Pipes must reach EOF or declare
coherent bounded framing. File/pipe reads and producer pulls are synchronous
application work, not asynchronous file I/O. The `/icy` route observes the literal ICY status flag on H1
via `status::with_shoutcast()`; it does not implement an audio subsystem.

### Operations and lifecycle

```sh
./examples/operations
curl -i "http://127.0.0.1:$PORT/short"
curl -i "http://127.0.0.1:$PORT/peer"
curl -i "http://127.0.0.1:$PORT/throw"
# Host stdin: stats, deny-loopback, allow-loopback, stop
./examples/lifecycle
curl -i "http://127.0.0.1:$PORT/wait"
# Host prints PENDING; enter resume to complete it, or drain then wait.
```

Operations retains all eight hook handles, short-circuits `/short` to 202,
adds `X-Hook: after`, observes exception/completion/status, and prints dispatched
diagnostic counts/gauges from `stats`. Connection cap is four; header and
response queues are bounded. `/peer` reports transport identity, independent
of forwarded headers. `deny-loopback` applies to subsequent keepalive requests;
restore access with stdin `allow-loopback`. Hooks perform no blocking I/O and
never log credentials/bodies. Close clients before observing active gauges;
retirement is asynchronous.

Lifecycle creates fresh one-shot signals per `/wait`, keeps only copied
signals in a synchronized registry, and prints `RETIRED` on RAII cleanup.
`/timeout` returns 408 after 50 ms; disconnecting `/cancel` prints `CANCELLED`
and retires its frame. POST `/stop` demonstrates handler-safe stop. `drain`
initiates a 500 ms budget, `resume` can release accepted work, and `wait` is the
coordinator's blocking ticket observation (`completed` or `deadline_expired`).
No detached application threads or retained exchange pointers are used.

### Protocols, external ownership and WebSocket

```sh
./examples/protocol_options                      # TLS-off
./examples/protocol_options CHAIN_PEM KEY_PEM    # TLS-on
./examples/external_event_loop 30
curl -i "http://127.0.0.1:$PORT/hello"
./examples/websocket_echo 30
curl -i "http://127.0.0.1:$PORT/echo"                # refusal without Upgrade
```

Protocol options prints actual validation/listen outcomes and ports, then
stops. The external host accepts an optional positive duration (the command above
uses 30 seconds); without it the process remains running until signaled. It
is the canonical readiness example: snapshot
interests carry generations, the host waits on descriptors/wakeups/deadlines,
and dispatches readiness through the public driver while owning the loop.
H1 `/echo` accepts text/binary WebSocket frames, awaits writable under
backpressure, and keeps the session alive through bounded close notification.
Choose application origin/subprotocol/size policies before exposing it.

For TLS programs supply a chain (leaf plus intermediates) and matching key for
`a.example`; trust roots belong to clients. From the configured TLS build:

```sh
./examples/http2 CHAIN_PEM KEY_PEM
# Set PORT to its first numeric stdout line. Enter stop/EOF to finish.
curl --cacert ROOT_PEM --resolve "a.example:$PORT:127.0.0.1" \
  --http2 "https://a.example:$PORT/hello"
./examples/http3 CHAIN_PEM KEY_PEM external
# First line: UDP port. Second: TCP_PORT <port>. Enter stop/EOF to finish.
```

H2 `/hello` returns `hello h2`; H3 `/hello` returns `hello native h3` and
`x-end: native-fin`. H3's TCP listener also serves H1/H2. Use `managed` or
`external`. Both protocols expose `/ws/echo` via Extended CONNECT and require
`harness.v1`. A curl build may lack HTTP/2 or HTTP/3; the focused independent
clients below cover Extended CONNECT/trailers without assuming curl support:

```sh
# From the repository root, create a dedicated client environment:
python3 -m venv /tmp/v3-example-clients
/tmp/v3-example-clients/bin/pip install -r test/integ/http2-client-requirements.txt \
  -r test/integ/http3-client-requirements.txt
V3_EXAMPLES_DIR="$PWD/build/native-tls/examples" V3_TLS_MODE=yes \
  /tmp/v3-example-clients/bin/python test/integ/examples_test.py \
  ExampleTests.test_http2_hello_and_extended_connect \
  ExampleTests.test_http3_hello_trailers_and_extended_connect_both_loops
```

The smoke runner starts/stops bounded children and verifies supplied repository
test CA material; those credentials are test fixtures, not deployment keys.
Missing optional protocol clients are explicitly skipped.

### TLS profiles and rotation

```sh
./examples/tls_profiles certificate A_CHAIN A_KEY B_CHAIN B_KEY
./examples/tls_profiles mtls CHAIN KEY CLIENT_ROOTS require
./examples/tls_profiles psk identity HEX_KEY
./examples/tls_profiles acme CHAIN KEY CHALLENGE_CERT CHALLENGE_KEY HEX_DIGEST
# All print PORT=...; enter stop. ACME stdin: publish, invalid, remove, stop.
```

Certificate mode explicitly selects host A by default and B through exact SNI.
Use hostname-verifying `curl --cacert ROOT_PEM --resolve ...` against `/hello`;
for mTLS add `--cert CLIENT_CHAIN --key CLIENT_KEY`. `request` also works in
place of `require`; `/hello` reports authenticated client metadata as
`verified=0|1`. PSK is HTTP/1-only: the bounded runtime rejects unknown identity,
checks callback cancellation/deadlines/identity/key caps, owns callback captures
through retirement; the coordinator explicitly requests stop and waits for
retirement. A
TLS1.2 independent client uses the same `HEX_KEY` and identity (PSK authenticates
through the shared key rather than an X.509 trust root):

```sh
printf 'GET /hello HTTP/1.1\r\nHost: a.example\r\nConnection: close\r\n\r\n' | \
  openssl s_client -connect "127.0.0.1:$PORT" -servername a.example \
  -tls1_2 -cipher PSK-AES128-GCM-SHA256 -psk_identity identity -psk "$HEX_KEY" -quiet
```

ACME receives application-supplied challenge material; the library does not
issue certificates. `HEX_DIGEST` is the 32-byte key-authorization SHA-256 digest
encoded as 64 hex digits and must match the challenge certificate's critical
`acmeIdentifier` extension. Expiration is one hour. `publish` validates before
publication; `invalid` preserves ordinary credentials; `remove` clears it.
Ephemeral ports prove publication/removal and **nonselection**. Actual selection
requires authentic TCP443, exact SNI and sole `acme-tls/1`; optionally append
`443` to the ACME server command on a host permitted to bind it, then run:

```sh
openssl s_client -connect 127.0.0.1:443 -servername a.example \
  -alpn acme-tls/1 -showcerts </dev/null
```

Inspect the presented challenge certificate and extension; ordinary self-signed
certificates are not ACME challenge fixtures.

```sh
./examples/tls_rotation CERTIFICATE_DIRECTORY managed 0
# First output is the port. Stdin: invalid, replace, stop (or EOF).
```

Rotation also accepts `external`. Its directory supplies `a.pem`, `a-key.pem`,
`b.pem`, `b-key.pem`, and `intermediate.pem`; chain files are composed from leaf
plus that intermediate. `/installed` returns `rotation-example`. `invalid`
prints `PRESERVED`; `replace` prints `REPLACED`. Established connections retain
their accepted generation, and new handshakes select replacement B. Test both
with the bounded smoke runner's rotation test. For separate new connections:

```sh
curl --cacert ROOT_PEM --resolve "a.example:$PORT:127.0.0.1" "https://a.example:$PORT/installed"
# Enter replace on the host, then:
curl --cacert ROOT_PEM --resolve "b.example:$PORT:127.0.0.1" "https://b.example:$PORT/installed"
```

### Installed consumers and source distributions

From the repository root, after the configured library build:

```sh
BUILD_DIR="$PWD/build/native" CXX=clang++ scripts/verify-installed-examples.sh
```

The verifier stages the install, copies all sources/helpers to a separate
temporary consumer directory, builds all fifteen with shared and static
linkage, checks depfiles for private/source/build header contamination, and
runs their bounded wire smokes against the staged package. `PYTHON` selects
the optional client environment and `V3_TLS_MODE=yes|no` can select the lane.
For a single installed consumer with installed pkg-config metadata:

```sh
clang++ -std=c++20 -pthread examples/auth.cpp \
  $(pkg-config --cflags --libs libhttpserver) -o /tmp/auth
```

`make dist` includes every indexed source/helper and the verification scripts;
archives build out of tree without `.git` or the original checkout.

## Historical legacy v2 sources

The following 38 sources are retained as unsupported historical v2 examples.
They are not part of `--enable-examples` on `v3` and require the legacy v2 API,
MHD/GnuTLS and original build configuration. Use the native examples above for
v3 development; no automatic v2 port is implied.

- [allowing_disallowing_methods.cpp](allowing_disallowing_methods.cpp)
- [args_processing.cpp](args_processing.cpp)
- [basic_authentication.cpp](basic_authentication.cpp)
- [benchmark_nodelay.cpp](benchmark_nodelay.cpp)
- [benchmark_select.cpp](benchmark_select.cpp)
- [benchmark_threads.cpp](benchmark_threads.cpp)
- [binary_buffer_response.cpp](binary_buffer_response.cpp)
- [centralized_authentication.cpp](centralized_authentication.cpp)
- [clf_access_log.cpp](clf_access_log.cpp)
- [client_cert_auth.cpp](client_cert_auth.cpp)
- [custom_access_log.cpp](custom_access_log.cpp)
- [custom_error.cpp](custom_error.cpp)
- [daemon_info.cpp](daemon_info.cpp)
- [deferred_with_accumulator.cpp](deferred_with_accumulator.cpp)
- [denied_ip_log.cpp](denied_ip_log.cpp)
- [digest_authentication.cpp](digest_authentication.cpp)
- [early_413.cpp](early_413.cpp)
- [empty_response_example.cpp](empty_response_example.cpp)
- [external_event_loop.cpp](legacy/external_event_loop.cpp)
- [file_upload.cpp](file_upload.cpp)
- [file_upload_with_callback.cpp](file_upload_with_callback.cpp)
- [handlers.cpp](handlers.cpp)
- [hello_with_get_arg.cpp](hello_with_get_arg.cpp)
- [hello_world.cpp](hello_world.cpp)
- [iovec_response_example.cpp](iovec_response_example.cpp)
- [minimal_deferred.cpp](minimal_deferred.cpp)
- [minimal_file_response.cpp](minimal_file_response.cpp)
- [minimal_https.cpp](minimal_https.cpp)
- [minimal_https_psk.cpp](minimal_https_psk.cpp)
- [minimal_ip_access_control.cpp](minimal_ip_access_control.cpp)
- [per_route_auth.cpp](per_route_auth.cpp)
- [pipe_response_example.cpp](pipe_response_example.cpp)
- [service.cpp](service.cpp)
- [setting_headers.cpp](setting_headers.cpp)
- [shared_state.cpp](shared_state.cpp)
- [turbo_mode.cpp](turbo_mode.cpp)
- [url_registration.cpp](url_registration.cpp)
- [websocket_echo.cpp](legacy/websocket_echo.cpp)

`protocol_options` also demonstrates typed H2/H3/QUIC capacity profiles, pure
`resolve_limits()` and immutable `effective_limits()` inspection with adequate
shared budgets. See [protocol capacity policy](../docs/v3-protocol-limits.md).
