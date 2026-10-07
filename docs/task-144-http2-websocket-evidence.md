# TASK-144 implementation evidence

Task worktree: `/Users/etr/progs/libhttpserver/.worktrees/TASK-144`.
Branch: `task/TASK-144`; base: `v3` at
`df41a0b935f6650ebf1c4f6207a75e20db39b22a`.
The registered worktree was clean before implementation. Task and index remain
In Progress; the runner owns commits and later validation/finalization.

## Implemented behavior

The request engine advertises SETTINGS_ENABLE_CONNECT_PROTOCOL=1. The standalone
connection keeps its default zero without adding a redundant tuple. Values other
than 0/1 and any 1-to-0 transition fail with connection PROTOCOL_ERROR; local
updates cannot revoke a queued advertisement. All six supported local tuples
fit the existing 45-byte control slot. Eligibility is captured when the first
frame-header octet arrives, survives fragmented HEADERS payloads and CONTINUATION,
and depends on the server advertisement, without requiring an ACK or client
capability setting.

Extended CONNECT retains CONNECT as the semantic method and normalizes its
origin path through the existing path derivation. Pseudo-fields stay outside
ordinary semantic fields. Ordinary CONNECT and unsupported extended protocols
remain refused; malformed field sections reset only their stream while HPACK
context continues. The separate HTTP/2 handshake shares origin, version,
subprotocol and policy validation with HTTP/1, preserving HTTP/1 check order.
A successful response is status 200 without END_STREAM, optionally with the
selected subprotocol. It has no HTTP/1 Upgrade/Connection fields, accept hash,
content length or negotiated extensions. Keys are not required. Finite-body
metadata is refused before tunnel commitment, leaving ordinary refusal available.

Each accepted stream owns a shared WebSocket driver and retains the normal HTTP/2
receive ring, independent windows, scheduler and drain unit. Partial codec
consumption leaves the exact suffix in the ring; deferred completed messages get
zero-length retries after application receive progress. Masking, message limits,
Ping/Pong and Close handling come from the shared codec. Weak, coalesced progress
observers marshal work through the serialized owner and ignore retired streams.
Handlers may move sessions to application ownership and finish independently.

Incoming windows debit full DATA payloads, including padding. Padding is reclaimed
immediately; tunnel octets are reclaimed when consumed from the receive ring.
Blocked codec admission stops new tunnel receive grants. The pre-ACK 65,535-byte
allowance is preserved. Outgoing DATA obeys both windows, peer frame size and the
16,384-byte scheduler quantum. Codec output is copied into one charged immutable
frame and consumed only as its payload retires; header writes consume no codec
bytes. Siblings and controls progress independently of blocked WebSockets.

Codec limits are clamped against route limits, engine caps and available aggregate
budgets. Admission reserves conservative input, output, metadata/control and
message-assembly bounds before driver construction. Reservations are distinct
from receive-ring and active-frame charges. Output admission leaves headroom for
a separately charged DATA frame. Failed reservation releases provisional charges
and preserves ordinary refusal. Reset and destruction return stream reservations.

Close bytes retire before an empty terminal DATA frame, which works without
positive credit. END_STREAM reaches the driver after retained input; missing
Close produces abnormal closure and stream CANCEL. Peer resets cancel pending
session operations once, remove semantic output/unexposed grants, preserve
borrowed frames and emit no echo. Connection EOF/destruction and deadline expiry
publish their transport reason before driver destruction. Reentrant callbacks
cannot destroy a stream or reuse an active frame during payload retirement.

Drain preserves the staged GOAWAY/PING barrier and sends Close 1001 on accepted
WebSockets; upgrades attempted after drain initiation are refused. Existing
stream units keep the drain outstanding through required wire retirement.
`ws_close` and `write_idle` are serviced by owner pumps, including `check_drain`
while the engine is running; this private composition seam has no transport timer.

## TDD receipts

Baseline: C++20 TLS-off build and `http2_frame`, `http2_connection`,
`http2_exchange` passed (3/3).

The native library was rebuilt before each test run. Behavioral RED receipts:

- SETTINGS: invalid setting 8 was accepted; 7 tests, 82 checks, 1 failure.
- Conversion: valid Extended CONNECT was rejected; 2 tests, 9 checks, 1 failure.
- Handshake/decision: absent sessions, wrong reset code and HTTP/2 decision
  delegation failures; handshake 5 tests/37 checks/6 failures, exchange decision
  5 tests/39 checks/5 failures.
- Codec/flow: absent input delivery, output and Pong; 3 codec tests/22 checks/
  3 failures and initial flow 3 tests/22 checks/4 failures. Reservation RED also
  detected uncharged codec storage and an incorrect successful admission.
- Lifecycle: missing END_STREAM, cancellation, abnormal reset, transport reason,
  drain Close and timeout; 6 tests/43 checks/10 failures.
- Reentrant output: callback output reused the retiring active frame; fixed by
  preserving the active-item ownership boundary through retirement.
- Reentrant drain: the stream reservation dropped to zero inside its own Close
  notification; 11 tests/118 checks/1 failure. Destruction now defers through the
  WebSocket pump guard as well as handler resumes.
- Fragmented opening: advertising between the start and end of a request frame
  incorrectly authorized CONNECT. Both incomplete frame headers and fragmented
  payloads are now covered.
- Reentrant receive-ring cancellation: a discarded borrowed prefix was consumed
  again after driver notification, underflowing the unread count. Consumption
  now bounds retirement to the retained ring contents.
- Tight output budget: successful codec admission consumed capacity needed for
  an active frame, producing connection failure. Admission now preserves wire
  headroom; refusal remains precommit.

Regression RED also caught deferred peer-reset cleanup and the redundant zero
SETTINGS tuple exceeding the existing 62-byte control-budget boundary. Immediate
safe cleanup and omission of the default-zero tuple restored the existing suites.

## Verification commands and scope

TLS-off configuration:

```sh
./bootstrap
mkdir -p build-v3
cd build-v3
../configure --disable-examples --disable-v3-tls \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
```

TLS-on uses the existing verified OpenSSL 3.5.9 provider:

```sh
mkdir -p build-on
cd build-on
../configure --disable-examples --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
```

For each configuration, rebuild `make -C <build>/src -j2`, build the selected
programs with `make -C <build>/test -j2 <selection>`, then run serially:

```sh
make -C <build>/test -j1 check-TESTS \
  check_PROGRAMS='<selection>' TESTS='<selection>'
make -C <build> check-local
```

Selection:

```text
http2_websocket_handshake http2_websocket http2_websocket_flow
http2_websocket_lifecycle http2_settings http2_frame http2_connection
http2_headers http2_exchange http2_streaming http2_flow_control
http2_fair_output http2_reset http2_rate_budget http2_drain
hpack_connection hpack_field_section websocket_codec websocket_session
websocket_session_race websocket_progress websocket_upgrade_decision
http1_websocket_handshake http1_websocket_upgrade websocket_drain
websocket_drain_race exchange_decisions exchange_routing
```

TLS-on adds `http2_tls_boundary`. Its real TLS adapter completes ALPN h2, delivers
server capability SETTINGS to the client, opens Extended CONNECT, observes the
response/receive grant, exchanges masked WebSocket DATA/Pong, and completes an
ordinary sibling GET. This proves the existing private TLS/HTTP2 composition.
The public listener still dispatches HTTP/1; listener cutover and TASK-145
independent-client/conformance work are separate.

ASan/UBSan uses a separate TLS-off configuration with
`CXXFLAGS='-std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'`
and `LDFLAGS='-L/opt/homebrew/lib -fsanitize=address,undefined'`. The six-suite
selection contains all four new suites plus `http2_reset` and `http2_drain`.
Tests run with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.

Changed C++ files are checked with `python3 -m cpplint --extensions=cpp,hpp
--headers=hpp <changed files>` and `python3 -m lizard -C 10 --warnings_only
<changed source files>`. Repository size and whitespace gates are
`scripts/check-file-size.sh` and `git diff --check`. Whole-repository complexity
still reports three untouched baseline functions (`dispatch_request`,
`valid_peer_pattern`, `accept_one`); changed-source complexity is clean.

BSD, Windows and other nonlocal checks were not executed; AGENTS.md assigns
those to CI and the v3 PR. No deployment or public-listener HTTP/2 claim is made.

## Final local results

- TLS-off selection: **28/28 suites passed**.
- TLS-on selection including the real TLS boundary: **29/29 suites passed**.
- ASan/UBSan selection: **6/6 suites passed**, with no sanitizer findings.
- The four new suites contain **29 cases and 328 checks**, all passing in each
  configuration.
- `check-local` passed in both TLS configurations, including standalone headers,
  examples, documentation, native linkage, staged install layout and hygiene.
- Changed-file cpplint and changed-source CCN <= 10 passed. The 500-SLOC repository
  size gate and `git diff --check` passed.
- LeakSanitizer probe with `ASAN_OPTIONS=detect_leaks=1` reported
  `AddressSanitizer: detect_leaks is not supported on this platform.` Leak checking
  was not executed; ASan/UBSan were run with leak detection disabled.

Terminal logs are at `/private/tmp/task144-regression-off-final.log`,
`/private/tmp/task144-regression-on-final.log`,
`/private/tmp/task144-asan-tests-final.log`,
`/private/tmp/task144-check-local-off-final.log` and
`/private/tmp/task144-check-local-on-final.log`. Per-suite receipts are also in
`<build>/test/<suite>.log`. No Git staging, commit, merge, deployment or worktree
cleanup was performed during implementation.
