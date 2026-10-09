# HTTP/3 WebSocket DATA and lifecycle adapter

TASK-172 connects the admitted Extended CONNECT session to the shared WebSocket
driver. The private HTTP/3 request adapter owns this integration; no public API or
QUIC scheduler changes are required.

DATA payloads feed the driver directly from the retained HTTP/3 event. HTTP/3
frame headers, unknown frames and QPACK fields never enter the codec. The event
cursor and existing body receipts advance by exactly `feed_result.consumed`.
Blocked suffixes retain their borrowed scratch storage. A blocked empty-message
header retains its event even when the cursor has reached the payload boundary;
capacity returning through `receive()` retries the codec before releasing that
event or delivering FIN.

A coalesced progress observer holds weak adapter ownership and posts to the owner
executor. Handler completion also retries a tunnel admitted after DATA was
already queued. The per-record receive guard prevents recursive feeding under an
inline executor, and pump guards defer semantic destruction through callbacks.
Observer allocation occurs before publishing the 200 HEADERS section, preserving
transactional admission under allocation failures.

Output keeps HEADERS ahead of DATA and stages at most one DATA section bounded by
`response_buffer_bytes`. The existing semantic storage reservation covers the
staging envelope. Driver output is copied and pinned, then submitted through the
existing reliable-retention path. Stream and connection allowances include the
HTTP/3 frame header. Only the newly retained WebSocket payload bytes call
`consume_output`; HTTP/3 header retention never frees codec capacity. Retention
can wake `writable()` before transmission or ACK, while drain completion still
waits for owned reliable information completions.

A valid-boundary FIN without WebSocket Close reports abnormal
`connection_closed`. Peer Close queues the reply before outbound FIN, including
when the peer supplies FIN immediately. Application Close retains the receive
half until its peer replies or transport termination intervenes. RESET reports
`cancelled`; STOP_SENDING and malformed WebSocket input reset only the affected
stream. Codec failures retain their own terminal reason. Disconnect and engine
destruction explicitly pass `connection_closed` before releasing the driver.
Cancellation becomes visible before callbacks, observers detach, and the shared
driver's terminal latch prevents duplicate notification. Trailing HEADERS on an
upgraded stream are refused locally; genuine connection-scoped HTTP/3 framing
errors retain their existing scope.

Drain initiates Close 1001 on every admitted tunnel and retains the existing
GOAWAY admission cutoff. Accepted ordinary siblings continue. The drain unit
survives session/handler retirement and reliable FIN completion. The existing
owner deadline cancels silent or credit-blocked peers with `timeout` exactly
once and reports `deadline_expired`. This adapter uses the existing engine drain
timer seam; production listener integration and nonlocal platform checks remain
with their assigned later tasks and CI.

## Verification

The new Littletest executables cover:

- `http3_websocket`: every bounded wire split, DATA fragmentation, ignored and
  empty DATA, coalesced messages, fragmented text/Ping/Pong and queued pre-upgrade
  DATA, with ordinary sibling responses.
- `http3_websocket_flow`: blocked nonempty and empty admission, exact consumption
  receipts, tiny stream-credit increments through both HTTP/3 header bytes,
  connection credit, queue backpressure, retention-before-transmission wakeups,
  bounded output sections and inline executor progress.
- `http3_websocket_lifecycle`: clean/abnormal FIN, local half-close, RESET,
  STOP_SENDING, invalid masking, disconnect/destruction, trailing HEADERS,
  reentrant callbacks during feed and output retention, multiple drain sessions,
  held reliable FIN completions and silent/credit-blocked deadline expiry.

Initial RED checks reported missing receive results, missing outbound DATA,
incorrect blocked-input receipts and absent Close/FIN/terminal notifications.
Once DATA and output passed, lifecycle RED checks drove explicit reason mapping
and drain Close initiation. The unchanged handshake allocation-failure test then
caught two response HEADERS sections when observer allocation happened after
committing 200; moving observer preparation ahead of admission made it pass.
Additional inline executor and retention tests passed as regression coverage.

Final local results on Linux with Clang C++20:

| Build | Focused suites | Result |
|---|---:|---|
| TLS off, `-O1 -g` | 19 | Passed |
| TLS on, OpenSSL 3.5.9, `-O1 -g` | 14 | Passed |
| TLS off, Clang ASan/UBSan | 6 | Passed |

The TLS-off suites are the three new executables, HTTP/3 handshake, settings,
headers, connection, exchange, streaming, cancellation, QPACK scheduling and
drain; WebSocket codec, session, session race and progress; and HTTP/2 WebSocket,
flow and lifecycle. TLS-on repeats the three new suites, HTTP/3 handshake,
settings, connection, exchange, streaming, cancellation, QPACK scheduling and
drain plus the three HTTP/2 WebSocket suites. The sanitizer suites are the three
new suites plus HTTP/3 handshake, cancellation and drain.

Build configuration and tests:

```sh
bash bootstrap
mkdir -p build-task172
cd build-task172
../configure CXX=clang++ --disable-examples --disable-v3-tls CXXFLAGS='-O1 -g'
make -C src -j6 libhttpserver_v3core.la
make -C test -j6 $focused_suites
make -C test check check_PROGRAMS="$focused_suites" TESTS="$focused_suites"
make -j6 check-local
```

TLS-on uses `build-task172-on`, `--enable-v3-tls` and
`PKG_CONFIG_PATH=/home/etr/progs/libhttpserver/build/tooling/provider/lib/pkgconfig`.
Sanitizers use `build-task172-san`, `CC=clang`, `CXX=clang++`, CXXFLAGS
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` and LDFLAGS
`-fsanitize=address,undefined`. Tests set `UBSAN_OPTIONS=halt_on_error=1` and
`ASAN_OPTIONS=detect_leaks=0`. A separate leak-enabled lifecycle run passed its
207 behavioral checks, then reproduced LeakSanitizer's fatal ptrace limitation;
address and undefined-behavior checks remained enabled in the passing gate.

Changed-file cpplint and `git diff --check` passed. `make -j6 check-local` passed
on retry: its first invocation hit an unchanged hooks-documentation pipeline's
broken-pipe failure; Doxygen explicitly skipped because it is unavailable locally
and remains CI-owned. A file-size probe reports only unchanged
`io_poll_backend.cpp` at 509 SLOC against 500. The task-local Lizard probe reports
only the unchanged `state::attach` at CCN 13; new and modified functions meet the
CCN 10 ceiling. PMD duplication checking was not run because PMD is unavailable.

Logs remain in the three ignored build directories (`final-focused-check.log`,
individual `test/*.log`, and sanitizer `leak-probe.log`), with cpplint/complexity
receipts in `/tmp/task172-*.log`. Task status remains In Progress for runner-owned
validation and finalization. BSD, Windows and other nonlocal checks were not run;
they belong to CI and the v3 PR under the repository workflow policy.
