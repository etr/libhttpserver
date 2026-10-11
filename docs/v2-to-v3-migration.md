# Migrating from v2 to the native v3 API

v3 replaces the MHD request/response model with a C++20 native engine and
protocol-neutral exchanges. There is **no v2 source or ABI compatibility shim**.
Include native headers directly or use `<httpserver.hpp>` / `<httpserverpp>`.
Version 3.0.0 ships only the native engine and uses SOVERSION 3. MHD and GnuTLS
are absent from the build and installed package. ABI-2 and ABI-3 versioned
runtimes coexist for separate executables; shared development-package filenames
prevent a promise of parallel development installations.

Start with [native examples](../examples/README.md), the
[native API page](v3-api.md), and [provider build instructions](native-tls-build.md).
The tables below distinguish preserved behavior from intentional migrations.
They summarize the [TASK-179 audit](task-179-behavior-parity.md) and its
[versioned parity inventory](../specs/architecture/v3/v2-parity-inventory.md).
That audit retains separate semantic, engine-frame, provider/BIO, and private
TCP/TLS/QUIC transport evidence; these are different kinds of proof.

## Availability in this checkout

| Capability | Public native API status | Example or evidence |
| --- | --- | --- |
| Plaintext HTTP/1.0 and HTTP/1.1 | Managed and external listeners available | `v3_routes`, `v3_external_event_loop` |
| HTTP/1.1 WebSocket Upgrade | Coroutine API available without legacy WS build flags | `v3_websocket_echo` |
| Certificate TCP TLS HTTP/1 listeners | Public credential store, managed/external listeners in TLS-on builds | `test/installed/v3_tls_consumer.cpp` |
| HTTP/2 and HTTP/3 listeners | TLS-on: TCP ALPN `h2`; explicit UDP QUICv1/TLS1.3 ALPN `h3`, managed/external | `v3_protocol_options`, `v3_http3` |
| HTTP/2 and HTTP/3 WebSocket | RFC8441/RFC9220 Extended CONNECT shares the bounded session API | Installed independent wire corpora |
| Common HTTP/1.1, HTTP/2, HTTP/3 handlers | Semantic exchange contract exercised for each version | `test/unit/v3_examples_test.cpp` |
| Certificate rotation | Public `tls_credentials::replace`, immutable accepted selections | `test/examples/v3_tls_rotation.cpp`, installed TLS runner |

`server_options::validate()` checks configuration semantics. TLS-on encrypted
listeners also require compatible published credentials. HTTP/3 requires a UDP
listener entry; TCP and UDP may share a fixed numeric port through two explicit
entries. Missing credentials and invalid mappings reject atomically. TLS-off keeps
the same declarations and typed `not_supported` outcomes. Public installed-client
evidence supplements the retained private transport and Extended CONNECT gates.

## Server construction and routing

| v2 call or behavior | v3 equivalent or removal | Behavior |
| --- | --- | --- |
| `create_webserver(port)`, `webserver`, `start(true)` | `server::server_options`, `add_listener`, `server::native_server`, `listen()`; the application owns its hosting lifetime | Migration: construction performs no I/O; typed validation precedes binding |
| `on_get`, `on_post`, value-returning lambda | `route_sync(http::method::known(...), path, handler, body_cap)`; handler returns `server::sync_response` | Restricted adapter admits/collects within a positive cap; oversized bodies get 413 before the application handler runs |
| `http_resource::render_*` | State captured by route handlers; synchronize shared mutable state | Migration: no native resource subclass required |
| Deferred callbacks, upload processing, streaming | `route(method, path, [](exchange&) -> task<void>)` | Coroutine reads/writes suspend; check typed results at every decision and await |
| Exact and `{name}` registrations; GET+HEAD family | `route(method)` or `route(http::method_set, pattern, handler)` | Captures use `exchange::path_args()`; HEAD retains representation length without body bytes |
| Literal tier always beats parameter tier | First registration-order full match | Migration: register `/users/me` before `/users/{id}` |
| `register_prefix`, catch-all | `route_prefix(method_set, pattern, handler)`; `/` is catch-all | Full matches win; deepest prefix wins, ties follow registration order |
| Regex route and `{name\|regex}` constraints | Removed from the native route table | Match in application handlers or a prefix wrapper; reject an invalid capture there |
| Route registration after start | Register before `listen()` | Native registrations return `invalid_state` after listen |

The [route example](../examples/v3_routes.cpp) registers GET and HEAD explicitly:
HEAD is not an implicit promise that every GET route also accepts HEAD.
Synchronous adapters have no streaming, upgrade, or suspension control. Use a
coroutine exchange for those operations. They run on worker threads, so blocking
application work still consumes a worker.

## Request data, forms, and cookies

| v2 surface | v3 equivalent | Behavior |
| --- | --- | --- |
| `http_request`, `get_method`, `get_path`, `get_headers` | `exchange::head()` with `request_method`, `route_path`, `raw_target`, `request_protocol`, `head_fields` | Raw escaped target/query retained; field occurrences retain order and case-insensitive lookup |
| Merged `get_arg` query/form/path map | Separate raw query, `path_args()`, and opt-in `forms::decode_urlencoded` / bounded form adapters | Intentional migration: decide source precedence and decoding limits explicitly |
| `get_content` | `admit_body(body_policy{cap})`, then `co_await body().collect(cap)` or incremental `read_some()` | Admission before reads; typed limits, cancellation and protocol errors |
| `get_footers` | `body().trailers()` after end-of-body | Trailers remain separate from initial fields |
| Multipart uploaded-file map and cleanup callbacks | `forms::read_multipart`, `part_sink`, `temp_file_part_sink`, `temp_file_options::should_keep` | Limits are snapshotted at invocation; exchange and part sink remain borrowed through task completion. Partial parts abort exactly once; disk files clean up on abort/destruction; keep callback runs once per completed file, throws mean remove; client names sanitized |
| `get_cookie` / response `with_cookie` | `http::parse_cookie_header`, `http::render_set_cookie`, ordered Cookie/Set-Cookie fields | Preserve repeated fields; no native request/response convenience aliases |

Form/query decoding is optional, bounded, and reports malformed encodings.
A query is the substring after `?` in `raw_target`; decoding it does not modify
the raw target. Body trailers are meaningful only after a completed body read.

Include `<httpserver/http/cookie.hpp>` directly or `<httpserver/http.hpp>`.
`http::cookie` owns raw strings and attributes; `parse_cookie_header` returns
owned pairs in token order. Iterate every `head_fields.all("cookie")` occurrence
in received order. Parsing trims SP/HT at token/name edges, splits at the first
`=`, strips one outer quote pair and skips malformed/empty-name tokens. It does
not URL-decode, trim leading value whitespace or remove interior controls.

`render_set_cookie(value, output)` validates before replacing output and returns
`http::outcome_code::invalid_argument` on invalid input. Append output to an
ordinary `Set-Cookie` field only on success, once per cookie. This replaces v2
fluent setters and throwing rendering; direct mutation is raw until rendering.
The renderer rejects CR/LF/NUL/semicolon, controls/whitespace/equals in names,
and commas in Domain/Path. Empty rendered names are invalid. Attribute order is
Expires, Max-Age, Domain, Path, Secure, HttpOnly, SameSite; None implies Secure.
Expires uses locale/timezone-independent GMT Unix seconds in years 0000..9999;
out-of-range times now fail explicitly instead of platform-dependent conversion.
Signed Max-Age, empty values, case and percent bytes remain unchanged. Existing
field budgets bound input; there is no cookie jar or cached request convenience.
The `http_cookie` suite preserves the three named capability-054 witnesses.

## Response ownership and producers

| v2 response factory | v3 equivalent | Behavior |
| --- | --- | --- |
| `http_response::string`, `binary`, `empty` | `sync_response`, or explicit `respond` / `start_response` plus `writer().write()` and `finish()` | Values pin Content-Length automatically; manual writes must supply coherent framing |
| File factory | `response_definition::reopen_file`, `co_await send_definition(...)` | Each send reopens the file and gets its own cursor; missing file fails before commit and the route boundary sanitizes it to 500 |
| Deferred producer | `response_definition::factory` with a fresh producer per send | Share immutable definition, never a mutable producer cursor across concurrent sends |
| Owned file / pipe | `response_definition::owned_file` / `owned_pipe` | Ownership transferred; one-shot claim rejects replay; pipe EOF ends an unbounded body |
| Borrowed binary storage | `response_definition::borrowed` with a nonempty `body_lease` | The lease must own the exact borrowed storage; an unrelated keeper cannot extend a temporary payload. Keep the definition and exchange alive through each send; immutable storage can be replayed |
| Iovec factory | No direct native iovec factory | Write bounded spans through the coroutine writer or supply a producer |
| Shared response with per-request headers | Immutable `response_definition` plus `response_overlay` | Separate shared definition from each request's fields and source state |

`respond()` is a one-shot terminal head decision. For a nonempty body,
`start_response()` commits the head before the writer is activated. Once committed,
a failure aborts the connection/stream; it cannot replace the response with a
second error page. `send_report` and body read/write results carry typed outcomes.

## Hooks, errors, and authentication

| v2 behavior | v3 equivalent or removal | Behavior |
| --- | --- | --- |
| Server hook bus | `native_server::hooks()` / `server::hook_bus` | Seven request phases plus listener `accept_decision`; retain the returned `hook_handle` or deliberately detach it |
| Per-resource hooks | Route-handler wrappers | Compose guards with handlers rather than relying on a native resource object |
| Connection-open/close and `body_chunk` hooks | Removed MHD notifications | Use transport peer snapshots, admission/completion hooks and the coroutine read loop |
| `not_found_handler`, `method_not_allowed_handler` aliases | `server_options::not_found_response()` / `method_not_allowed_response()` factories | Default 404/405 bodies retained; 405 includes merged Allow; invalid/throwing factories degrade to defaults |
| `after_handler` replaces whole response | Head-commit hook mutates status/fields | For streaming it runs at commit during the task; no full native response replacement |
| Uncaught handler exception | Contained at route boundary; `handler_exception` hook | Before commit, sanitized empty 500; after commit, abort; throwing exception hooks preserve containment |
| `response_sent`, `request_completed` | Native response/completion contexts | Exactly-once lifecycle; completion carries typed outcome rather than MHD completion codes |
| Basic checks and unauthorized factory | `auth::basic_auth_policy::check`, `make_basic_guard` | Challenge status/fields/body belong to the verdict; compose per route |
| Digest checks, centralized auth and skip paths | `auth::digest_auth_policy::check`, `make_digest_guard`, route wrappers | Bounded nonce/replay ledger, algorithm choice and HA1 source; nonce entropy failure is 503, stale nonce challenges support retry |

Status codes are HTTP outcomes; `http::outcome_code` describes library operations.
Handle `invalid_argument`, `invalid_state`, `limit_exceeded`, `not_supported`,
`cancelled`, `connection_closed`, `timeout`, `would_deadlock`, `peer_refused` and
`protocol_error` according to the call's contract. Diagnostic text is not an
application protocol. Avoid converting every failure into a new HTTP response
when an exchange has already committed or disconnected.

## Peer policy and shutdown

| v2 behavior | v3 equivalent | Behavior |
| --- | --- | --- |
| `default_policy`, `allow_ip`, `deny_ip` | `server_options::peer_policy()` seed and live `native_server::peer_policy()` | Typed outcomes and `peer_refusal`; exact literal/CIDR and trailing IPv4 wildcards |
| Arbitrary middle IPv4 wildcards | Rejected | Convert to CIDRs or trailing wildcards |
| Peer identity from transport | Immutable `exchange::peer()` snapshot | IPv4-mapped normalization; forwarded headers never replace transport identity; unspec identity has explicit policy rules |
| One admission decision per connection | Policy revalidated for every request | In-flight reply finishes; next denied request has no application response (HTTP/1 EOF, H2/H3 stream reset in private transport evidence) |
| `stop()` called inside handler | `request_stop()` | Nonblocking initiation safe in handlers; cancels active exchanges, including the caller; no reply is guaranteed |
| Controlling-thread shutdown | `stop()` | Joins workers; call outside counted handler work |
| Graceful drain | `begin_drain(milliseconds, drain_ticket&)`, controlling-thread `ticket.wait(result)` | Deadline-bound completed/expired result; counted handler waits return `would_deadlock` |
| `get_fdset`, `run`, `run_wait`, `add_connection` | External `readiness_driver::interests()` / `dispatch()` | Preserve generation tokens, host deadlines and worker wakeups; see `v3_external_event_loop` and the external-loop contract |

## Protocol framing and TLS migration

HTTP/1.0 known-length replies can retain requested keepalive. Unknown-length
pipe/deferred bodies use EOF framing and close instead of HTTP/1.1 chunking.
H2/H3 use lowercase field names, numeric status, DATA framing and stream
completion. Remove Connection and nominated connection-specific fields;
initial fields and trailers remain separate. HTTP/1 pipelining becomes correlated
streams; connection-wide response ordering is not an H2/H3 contract.

`http::status::from_code(200).with_shoutcast()` retains the literal ICY status line on HTTP/1. On H2/H3 it
maps to numeric 200 with the same body. This provides the response token, not a
new SHOUTcast audio/metadata feature. WebSocket uses HTTP/1.1 Upgrade (101 and
Accept); private H2/H3 transports use Extended CONNECT (200 without those fields).
HTTP/1.0 WebSocket and cleartext certificate/TLS cells are protocol applicability
exclusions, not failed parity observations.

Native TLS selects optional OpenSSL at build time with `--enable-v3-tls`; the
current build gate requires stable OpenSSL **3.5.x patch 9 or later** and its QUIC
TLS callbacks. Public certificate values go through `server::tls_credentials`;
`listener_options::credentials` associates the store. `server_options::tls()`
selects provider/profile policy. Public host values accept initial mTLS trust and
request/require mode, or bounded external PSK for HTTP/1. The application owns the
shared lookup runtime through actual callback retirement; server stop requests
cancellation without joining application code. Public ACME publication/removal
uses application-supplied material and exact SNI/sole `acme-tls/1` on TCP443.
Post-handshake auth remains unavailable; see the [public TLS lifetime contract](native-tls-build.md).

The [public listener rotation example](../test/examples/v3_tls_rotation.cpp)
uses `server::tls_credentials::replace()`: successful publication replaces the
active generation, accepted sessions retain their contexts, and failed
replacement leaves the active publication unchanged. It uses only installed
public headers. `check-v3-public-tls.py` verifies selected certificates and held
handshakes/connections across replacement through installed shared/static
libraries. The private rotation suite remains supporting lifetime evidence.

BSD, Windows and other nonlocal runs belong to CI and the v3 PR; the
[TASK-183 local evidence](task-183-migration-evidence.md) records what ran locally.


TASK-193 adds public certificate TCP TLS through
[`server::tls_credentials`](native-tls-build.md#public-certificate-credentials-and-native-tcp-tls).
Associate a shared store with each TLS listener and publish PEM values with
`replace`; accepted sessions retain immutable selections during replacement.
Managed and external loops support the same HTTP/1 routes. TASK-194 extends
installed witnesses to mTLS/PSK/ACME and live listener rotation. H2/H3 public
listeners remain subsequent repair tasks; unsupported combinations retain typed
pre-bind outcomes. TLS-off exposes the same declarations and typed unavailable
credential/runtime operations.
