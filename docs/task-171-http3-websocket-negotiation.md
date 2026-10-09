# TASK-171 HTTP/3 WebSocket negotiation

HTTP/3 now advertises `SETTINGS_ENABLE_CONNECT_PROTOCOL=1`. A peer must send
that value in a completely validated SETTINGS frame before the first byte of
an opening HEADERS frame reaches the connection parser. Missing or zero values
disable Extended CONNECT; values above one and duplicate settings cause
`H3_SETTINGS_ERROR` (0x109). A later malformed pair cannot publish partial state.
Opening eligibility survives fragmented frame headers, payloads and QPACK
blocking. Ignored frames preceding the opening HEADERS do not freeze it early.
Ordinary requests still work before peer SETTINGS.

A WebSocket route is an ordinary registered CONNECT coroutine route that calls
`co_await x.upgrade(options)`. The semantic head retains CONNECT, exposes the
canonical origin path for matching and the effective authority as Host, and
excludes pseudo-headers from ordinary fields. The original two-argument private
HTTP/3 converter still rejects CONNECT; the request engine uses a metadata
overload. No legacy WebSocket registry or new public routing API is involved.
Unregistered paths receive 404; CONNECT routes may respond normally without
upgrading. Premature or malformed CONNECT, including ordinary CONNECT, resets
only its stream with `H3_MESSAGE_ERROR` (0x10e).

The HTTP/3 handshake adapter reuses policy, origin, version and subprotocol
validation with HTTP/1 and HTTP/2. HTTP/2 and HTTP/3 share the finite-body and
negotiation-field checks without changing HTTP/2 eligibility. Unsupported
protocols and schemes receive typed refusals; finite-body metadata, invalid
options and policy failures leave ordinary response handling available. An
unsupported version suggests 426 and `Sec-WebSocket-Version: 13`. Subprotocols
follow server preference. Extension offers are ignored, with no extension
response field, HTTP/1 upgrade headers or accept hash.

Successful admission returns an application session and queues exactly one
stream-local QPACK HEADERS section with status 200, optionally a selected
subprotocol, and no FIN. Internal codec caps match the established HTTP/2
bounds. Session storage is reserved against the QUIC pool's ancestor budgets
before allocation; QUIC wire storage uses the separately charged pool envelope.
Allocation and HEADERS refusal release provisional charges and leave an error
response available. Cancellation and destruction release the owned driver and
reservations. The handler may finish while the upgraded stream stays owned;
repeated upgrade or response decisions are refused. Sibling requests progress.
Admission also rejects draining, cancelled or disconnected streams and a known
received FIN, including FIN received beyond a reassembly gap.

Allocation fault injection exposed two necessary shared fixes: `fields::entries()`
can allocate its lazy view cache, so it now propagates allocation failures instead
of promising `noexcept` and terminating. Destroying a moved-from WebSocket session
now skips cancellation, avoiding an allocation after committing success. The
focused suite covers every allocation position through driver/session and success
HEADERS admission, with typed refusal and complete reservation rollback.

This slice covers negotiation and session admission. TASK-172 owns DATA delivery,
codec output framing, flow-control progress and FIN/reset/drain close semantics.
This document does not claim WebSocket message transport over HTTP/3.

## Local verification

Clang C++20 builds passed with TLS disabled and with the existing genuine
OpenSSL 3.5.9 provider. The TLS-off run passed 20 suites, 214 cases and 5,550
checks. The TLS-on run passed the 12 planned suites, 145 cases and 2,586 checks.
The new handshake suite passed 16 cases and 633 checks in both configurations.

The Clang ASan/UBSan build passed the handshake, SETTINGS, QPACK scheduling,
cancellation, semantic-fields and WebSocket-session suites: 80 cases and 1,746
checks. `UBSAN_OPTIONS=halt_on_error=1` was set. LeakSanitizer cannot run under
this environment's ptrace supervision; its configure probe reproduced the fatal
diagnostic. Only leak collection was disabled with `ASAN_OPTIONS=detect_leaks=0`.

Commands used from the task worktree:

```sh
bash bootstrap
mkdir -p build-task171
cd build-task171
../configure CXX=clang++ --disable-examples --disable-v3-tls CXXFLAGS='-O1 -g'
make -C src -j6 libhttpserver_v3core.la
focused_suites='http3_websocket_handshake http3_settings http3_headers
http3_connection http3_exchange http3_streaming http3_cancellation
http3_qpack http3_qpack_scheduling http3_drain websocket_upgrade_decision
http2_websocket_handshake http_semantic_fields websocket_session
websocket_session_race websocket_progress http1_websocket_handshake
http2_websocket http2_websocket_flow http2_websocket_lifecycle'
make -C test -j6 $focused_suites
make -C test check check_PROGRAMS="$focused_suites" TESTS="$focused_suites"
make -j6 check-local
```

TLS-on used a separate `build-task171-on` directory with `--enable-v3-tls` and
`PKG_CONFIG_PATH=/home/etr/progs/libhttpserver/build/tooling/provider/lib/pkgconfig`.
The sanitizer directory was `build-task171-san`, configured with `CC=clang`,
`CXX=clang++`, CXXFLAGS `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`
and LDFLAGS `-fsanitize=address,undefined`, with leak collection disabled during
configure and test execution.

RED receipts show invalid setting acceptance, missing CONNECT conversion and
HTTP/3 exchange delegation before their implementations. Negotiation initially
created no session. Allocation fault injection then exposed termination during
lazy field-view construction and moved-from session destruction; both fixes
made the exhaustive admission fault test green. A separately observed RED test
showed a known FIN beyond a receive gap incorrectly admitting a session; the
live-stream guard fixed it. Local control bootstrap accounting tests now derive
their expected reservation and flow sizes from the enlarged SETTINGS prefix.

`make -j6 check-local`, changed-file cpplint and `git diff --check` passed. The
Doxygen subgate explicitly skipped because Doxygen is unavailable locally; CI's
documentation gate owns that evidence. An additional file-size probe reports only
the unchanged `io_poll_backend.cpp` at 509 SLOC against 500. The optional Lizard
probe, using existing `/tmp/task166-lint-tools`, reports no new CCN-over-10
functions; unchanged baseline offenders remain. Optional CPD duplication checking
was not run because PMD is unavailable; the CI lint lane owns tool provisioning
and that receipt. These additional repository-wide probes do not replace the
passing task-local checks.

Detailed logs remain at `/tmp/task171-*-final-*.log`,
`/tmp/task171-check-local.log`, `/tmp/task171-cpplint-final.log` and the individual
Automake logs in the three ignored build directories. The task and task index
remain In Progress for runner-owned validation and finalization. BSD, Windows
and other nonlocal checks belong to CI and the v3 PR; they were not run locally.
