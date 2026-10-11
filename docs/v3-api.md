\page native_v3_api Native v3 API and migration

The native C++20 API uses direct headers under httpserver/; the v2 umbrella
remains a legacy reference. Read the [v2-to-v3 migration guide](https://github.com/etr/libhttpserver/blob/v3/docs/v2-to-v3-migration.md)
for behavior changes and the [example index](https://github.com/etr/libhttpserver/blob/v3/examples/README.md) for commands.
There is no v2 source/ABI compatibility shim.

\section native_v3_configuration Configuration and hosting

\ref httpserver::server::server_options defines listener endpoints, protocol
selection, budgets, timeouts, worker concurrency and loop ownership.
\ref httpserver::server::native_server validates before binding and owns shutdown.
Plaintext HTTP/1.0 and HTTP/1.1 listeners are available. TLS-on builds also serve
public HTTP/1 certificate, initial mTLS and eligible external-PSK listeners.
Certificate and initial mTLS listeners also serve HTTP/2 when enabled and negotiated
with TLS ALPN `h2`. HTTP/3 listeners still return not_supported before binding.
HTTP/2 has no cleartext upgrade or prior-knowledge listener.
Provider availability from \ref httpserver::query_features is a separate fact.

\include v3_protocol_options.cpp

External hosts drive \ref httpserver::server::readiness_driver interests,
generations, wakeups and deadlines. See the [external loop contract](https://github.com/etr/libhttpserver/blob/v3/docs/external-loop-contract.md).

\include v3_external_event_loop.cpp

\section native_v3_routes Routes, exchanges and bodies

Register routes before listen. \ref httpserver::server::route_registry has a
single registration-order full-match class; register literal siblings first.
\ref httpserver::server::make_sync_route adapts bounded value handlers.
\ref httpserver::exchange is the coroutine decision surface; admit before
reading with \ref httpserver::body_reader, commit before writing with
\ref httpserver::response_writer. Path captures and trailers remain separate.
\ref httpserver::response_definition and \ref httpserver::send_definition
provide owned/reopened/leased body sources and per-send producers.

\include v3_routes.cpp
\include v3_route_handlers.hpp

\section native_v3_cookies Owned cookie values

Include <httpserver/http/cookie.hpp> directly or through <httpserver/http.hpp>.
\ref httpserver::http::parse_cookie_header returns owned raw cookie pairs;
iterate every ordered Cookie field. Construct or modify
\ref httpserver::http::cookie attributes, then call
\ref httpserver::http::render_set_cookie and append the output as a separate
Set-Cookie field only on success. Invalid input yields invalid_argument and
leaves output unchanged. The migration guide documents exact parsing,
canonical attribute ordering, SameSite=None and bounded GMT Expires behavior.

\section native_v3_websockets WebSocket sessions

\ref httpserver::exchange::upgrade returns a typed rejection or owning
\ref httpserver::websocket::session. Check try_send backpressure and await
writable; keep the session alive through close notification. The public example
uses HTTP/1.1 Upgrade; private H2/H3 evidence uses Extended CONNECT.

\include v3_websocket_echo.cpp

\section native_v3_shutdown Shutdown and outcomes

request_stop() initiates shutdown without waiting and is handler-safe. stop()
joins workers on the controlling thread. begin_drain() yields a
\ref httpserver::server::drain_ticket with a deadline-bound result; waiting from
counted work returns would_deadlock. \ref httpserver::http::outcome and
\ref httpserver::http::outcome_code describe operation results.

\ref httpserver::server::hook_bus, \ref httpserver::server::peer_policy,
\ref httpserver::auth::basic_auth_policy, \ref httpserver::auth::digest_auth_policy,
and the bounded forms APIs replace the corresponding v2 conveniences.

\ref httpserver::server::tls_credentials copies share an immutable-generation
store attached to each TLS listener. replace validates before atomic publication;
selected connections retain their generation, and incompatible bound-policy
changes reject. Public host values carry mTLS trust/request/require policy or
external-PSK lookup configuration. The application owns the bounded shared lookup
runtime through actual callback retirement; native stop never joins application
lookups. Public ACME publish/remove accepts application-supplied material and
selects only exact SNI plus sole acme-tls/1 on authentic TCP443.

The installed public listener example at test/examples/v3_tls_rotation.cpp serves
requests across valid/rejected live replacement. Read the [TLS usage and lifetime
contract](https://github.com/etr/libhttpserver/blob/v3/docs/native-tls-build.md).
TLS-off retains the same declarations and typed unavailable operations.
The [TASK-179 audit](https://github.com/etr/libhttpserver/blob/v3/docs/task-179-behavior-parity.md) distinguishes semantic,
frame, provider and private TCP/TLS/QUIC observations.

## Explicit UDP HTTP/3 listeners

A listener entry owns one transport. Append `server::listener_transport::udp` to the existing aggregate to bind QUIC; TCP remains the default:

```cpp
options.protocols().enable(http::protocol::http_3);
options.tls().provider = server::tls_provider::system_default;
options.tls().profile = server::tls_profile::certificates;
options.add_listener({"127.0.0.1", 0, true, credentials,
                      server::listener_transport::udp});
```

Publish a certificate host with `h3` ALPN through the ordinary public credential store and register the ordinary public routes. `get_bound_port(index)` reports that UDP entry's actual port. To serve TCP and UDP on the same fixed port, configure two entries with different transports. All endpoints bind before any becomes active; partial failure rolls back every port. H3 without explicit UDP and UDP without H3/TLS fail before bind. TLS-off builds retain these declarations and return typed `not_supported`. External PSK remains HTTP/1-only.

The native path supports authenticated QUICv1/TLS1.3 routes, incremental bodies, ordered trailers and independent concurrent request streams. Native RFC9220 and comprehensive HTTP/3 parity/rotation/staged drain are subsequent v3 tasks. `request_stop()` is immediate; `stop()` and destruction retain callback services until pending completions and protocol close expiry, including when an external host loop has stopped dispatching.
