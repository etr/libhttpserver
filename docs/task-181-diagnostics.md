# Bounded native diagnostics

Install a shared diagnostic channel before constructing the server:

```cpp
#include <httpserver/server/server.hpp>
#include <httpserver/server/diagnostics.hpp>

httpserver::server::diagnostics channel([](const auto& event) {
    // Consume library enums and scalar identifiers on this application's thread.
});
httpserver::server::server_options options;
options.diagnostic_channel() = channel;
httpserver::server::native_server server(options);
// Configure routes/listeners and run the server as usual.
channel.dispatch(32);
auto counters = channel.counters();
```

Call `dispatch()` regularly on an application thread to receive callbacks.
Each call consumes at most its requested maximum and the queue size observed
at entry. Slow callbacks occupy that thread; producers never invoke callbacks,
post executor work, format messages, or allocate event storage. They use a
128-record fixed queue and `try_lock`; full queues and contention drop records
and increment `dropped_events`. Event totals remain accurate through drops.
Concurrent or recursive dispatch returns `invalid_state`. Callback exceptions
increment `callback_failures` and do not propagate into engine outcomes.
Callbacks can inspect counters and request server stop. Application captures
must remain valid while dispatch can occur. Destruction never dispatches;
a retained channel can deliver queued events after server destruction.
A default channel disables instrumentation. Constructing a channel with an
empty `diagnostics::callback` enables counters without retaining event records.

Records contain only library enums, typed outcomes, numeric connection,
stream and listener identifiers, resource kinds, and numeric quantities.
Resource kinds apply to refusal events; IDs are zero when a seam has no ID.
Protocol is absent on global accounting, connection admission and drain scopes.
No request objects, URLs, query strings, bodies, Authorization, cookies,
addresses, raw QUIC identifiers/tokens, TLS credentials, SNI, certificate or PSK
identities, or provider error messages can enter this record type.

`events` is indexed by `diagnostic_kind`. Cumulative fields saturate at the
64-bit maximum. `active_connections`, `active_streams`, and
`charged_response_queue_bytes` reflect committed hierarchical budget charges;
response charges include retained storage and metadata, not just unsent
payload. Fields are individually atomic and a concurrent snapshot is
approximate. Child charges and refusals are observed once, with admission
rollback excluded from gauges. A channel shared across roots aggregates them.
Attach private budgets before deriving children or publishing them.

HTTP/1 accepts/rejects/closes and exchanges, private HTTP/2 and HTTP/3 semantic
streams, QUIC promotion/refusal, TCP/QUIC handshake terminals, limits, flow
stall transitions, and shared drain transitions enqueue observations. HTTP/2
and HTTP/3 diagnostics do not expand native listener protocol support.
A drain emits start and exactly one completion or expiry, even without a
waiter; expiry retains the pre-cancellation remaining count in `quantity`.
TLS cancellation/closure is excluded from handshake-failure totals.

Local build reproduction (Clang 18.1.3, C++20):

```sh
./bootstrap
mkdir -p build/task181-off build/task181-on
(cd build/task181-off && ../../configure --disable-examples --disable-v3-tls \
  CXX=clang++ CXXFLAGS='-std=c++20 -O1 -g')
(cd build/task181-on && ../../configure --disable-examples --enable-v3-tls \
  CXX=clang++ CXXFLAGS='-std=c++20 -O1 -g' \
  V3_TLS_CFLAGS='-I/home/etr/progs/libhttpserver/build/tooling/provider/include' \
  V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto -ldl -pthread')
```

For each build, rebuild `src` first with `make -C build/task181-off/src -j2`
(substitute `task181-on` for TLS). Build the following focused targets in `test`,
then run `make -C build/task181-off/test check-TESTS check_PROGRAMS= TESTS='...'`
with the same target names:

```text
server_diagnostics diagnostic_dispatch consumer_v3_diagnostics server_budgets
connection_engine native_server drain_scope http2_streaming http2_flow_control
http2_drain http3_streaming http3_cancellation http3_drain resource_limits
v3_header_hygiene v3_native_linkage
```

TLS-on also includes `tls_io tls_io_race tls_io_loopback quic_tls_session
quic_tls_handshake quic_server_admission`. Run `make -C build/task181-off
check-v3-native-linkage` in both builds.

BSD, Windows, macOS and other nonlocal checks belong to CI and the v3 PR.

Local evidence (2026-10-09): TLS-off focused run passed all 16 targets; TLS-on passed all 22.
The native linkage audit passes in both configurations.
Behavioral coverage includes queue reuse and overflow totals, concurrent
producers/dispatch, recursive dispatch and callback exceptions, retained-channel
lifetime, ancestor budget refusal rollback, real HTTP/1 I/O while a callback is
stalled, HTTP/2 and HTTP/3 completion/reset/flow transitions, and drain terminal
arbitration. Request fixtures carry secret URL, body, Authorization and cookies;
records' public scalar type excludes those inputs. TLS fixtures use real provider
handshakes and verify success, provider failure and cancellation accounting.
A recoverable QUIC reassembly refusal leaves handshake-failure totals unchanged.

Changed C++ files pass cpplint (`--linelength=200`); task-local Lizard comparison
finds no new function above CCN 10. Repository warning-suppression, server-ready,
tautological-assertion, deprecated-cookie, skip-exit-code and fence-balance gates
pass. `git diff --check` passes. Full GCC 13 library build remains blocked by
unchanged `server_options::peer_policy()` name lookup; standalone diagnostic tests
pass with GCC using `-Wall -Wextra -Werror` (7 tests, 30 checks). Existing whole-source size/complexity violations remain unchanged
(`io_poll_backend.cpp` is 509 SLOC against 500). PMD is unavailable locally, so
`lint-duplication` remains unexecuted; the CI lint lane owns that dependency and
check. These are recorded limitations, without replacement gates.
