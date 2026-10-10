\page native_v3_api Native v3 API and migration

The native C++20 API uses direct headers under httpserver/; the v2 umbrella
remains a legacy reference. Read the [v2-to-v3 migration guide](https://github.com/etr/libhttpserver/blob/v3/docs/v2-to-v3-migration.md)
for behavior changes and the [example index](https://github.com/etr/libhttpserver/blob/v3/examples/README.md) for commands.
There is no v2 source/ABI compatibility shim.

\section native_v3_configuration Configuration and hosting

\ref httpserver::server::server_options defines listener endpoints, protocol
selection, budgets, timeouts, worker concurrency and loop ownership.
\ref httpserver::server::native_server validates before binding and owns shutdown.
Plaintext HTTP/1.0 and HTTP/1.1 listeners are available. Public TLS/H2/H3 listeners
return not_supported before binding, even when semantic validation succeeds.
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

Certificate rotation has no public installation API. The separately labeled
repository-internal demonstration at test/examples/v3_tls_rotation.cpp uses an
uninstalled private header. It is not included as a public API example here.
The [TASK-179 audit](https://github.com/etr/libhttpserver/blob/v3/docs/task-179-behavior-parity.md) distinguishes semantic,
frame, provider and private TCP/TLS/QUIC observations.
