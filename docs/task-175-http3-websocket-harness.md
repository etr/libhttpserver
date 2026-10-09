# Independent WebSocket-over-HTTP/3 harness (TASK-175)

The explicit network gate uses pinned **aioquic 1.3.0** for QUIC v1, TLS 1.3,
QPACK and HTTP/3. It verifies the ephemeral fixture CA with SNI `localhost`
and ALPN `h3`; a separate wrong-CA connection must fail certificate verification.
No early data is used. The Python client imports no server codecs, native test
fixtures or libhttpserver bindings. Its RFC 6455 masked-frame generator and
incremental server-frame observer use only Python's standard library. The
installed library gains no dependencies or public API changes (DR-V3-001).

The client waits for received `SETTINGS_ENABLE_CONNECT_PROTOCOL=1` before
RFC 9220 Extended CONNECT, requires status 200 and offered subprotocol
`harness.v1`, and rejects negotiated extensions and HTTP/1 Upgrade fields.
It offers `permessage-deflate` to exercise explicit extension refusal.

The echo tunnel exercises masked text, binary bytes including NUL/non-UTF-8,
fragmented UTF-8 with an interleaved Ping, payload-matching Pong, WebSocket
headers/payload split across H3 DATA sends, multiple frames in one DATA send,
continued exchange after Ping, and Close 1000 with terminal FIN. The observer
checks exact payloads, opcode, FIN and unmasked server output independently of
DATA boundaries. An unmasked client frame must terminate just that stream,
produce one `protocol_error` callback, and leave an HTTP health sibling usable.

A second verified connection holds `GET /hold` through both scenarios:

1. An echo tunnel receives STOP_SENDING and RESET_STREAM with
   H3_REQUEST_CANCELLED (`0x10c`). The client sees a native reset, the fixture
   records one `cancelled` close callback, and a health sibling completes.
2. A flood tunnel reaches its advertised 4096-byte receive-credit boundary.
   Its server snapshot must show an actual backpressured send and outstanding
   `writable()` wait. Another health sibling completes while credit remains
   withheld. Restoring credit produces all 64 ordered binary messages, each
   containing 8192 repetitions of its index byte, then a clean close.
3. The original held HTTP request is released by exact connection/stream
   identity and must complete with status 200.

The native qlog must order exhausted flood credit, sibling FIN, a higher
MAX_STREAM_DATA for that flood stream, and flood FIN. Fixture held/released
observations bracket reset and backpressure. A timer or client boolean alone
cannot satisfy this gate.

## Bounds and adapter seams

The fixture allows four connections with a 15-second owner watchdog. The gate
uses three connections, per-case deadlines and a 40-second process deadline.
It caps diagnostic output, queued events, commands and observations. The client
caps pending streams at 16, frame queues/counts at 256, server payloads at 8192
bytes, buffered wire at 1 MiB and HTTP sibling bodies at 64 KiB.

Shared fixture routes use public exchange/session operations, 8192-byte incoming
messages, one incoming/outgoing message and 8202 bytes of outgoing capacity.
The extra ten bytes accommodate the server admission policy's conservative
frame overhead. Every `try_send` disposition is checked; backpressure suspends
and retries. The flood waits for its last frame to leave the codec before
`close()`, which can otherwise discard queued application data. Neither
`writable()` nor an accepted send proves peer delivery; the client independently
checks every received byte. Sessions remain alive through their close callback,
and observations remain until fixture teardown to detect duplicate callbacks.

The pinned test adapter reuses TASK-174's `_write_stream_limits` seam to withhold
only the selected stream's receive-credit updates. ACKs, connection credit
(16 MiB), and ordinary streams remain active. `SemanticClient` accepts an H3
factory so only one set of critical streams is opened. The SETTINGS observer
calls native `_handle_control_frame` first, records validated
`_received_settings`, and appends `http:settings_received` to the same qlog.
This is an explicit client observation added by the test adapter; other HTTP
headers and transport credit/reset/FIN events come from aioquic's native logger.
The version and seams fail closed if unavailable.

The external gate exposed a server cancellation-reason defect: STOP_SENDING
`0x10c` arriving before RESET_STREAM reported `protocol_error`. The focused
native lifecycle regression covers both orders and exactly one notification.
The repair preserves `cancelled` for that peer error code while retaining the
existing reasons for local protocol errors and other STOP_SENDING codes.

## Reproduction

From an isolated v3 task worktree, provision the independent client explicitly:

```sh
python3 -m venv build/http3-venv
build/http3-venv/bin/pip install -r test/integ/http3-client-requirements.txt
bash bootstrap
mkdir -p build/task175-on build/task175-off
```

Configure TLS on with an OpenSSL 3.5 provider. The local verified provider used
here resides under `/home/etr/progs/libhttpserver/build/tooling/provider`:

```sh
cd build/task175-on
../../configure --enable-v3-tls --disable-examples CC=clang CXX=clang++ \
  CXXFLAGS='-O1 -g' \
  V3_TLS_CFLAGS='-I/home/etr/progs/libhttpserver/build/tooling/provider/include' \
  V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'
make -C src -j4 libhttpserver_v3core.la
make -C test -j4 http3_udp_fixture http3_network_owner
cd ../..
```

Use the appropriate provider paths for another environment. Configure the
TLS-disabled build independently:

```sh
cd build/task175-off
../../configure --disable-v3-tls --disable-examples CC=clang CXX=clang++ \
  CXXFLAGS='-O1 -g'
make -C src -j4 libhttpserver_v3core.la
cd ../..
```

Build and run the seven focused native suites in each configuration:

```sh
for configuration in on off; do
  suites='http3_websocket_harness http3_websocket_handshake http3_websocket http3_websocket_flow http3_websocket_lifecycle http3_settings http3_cancellation'
  make -C "build/task175-$configuration/test" -j4 $suites
  make -C "build/task175-$configuration/test" check \
    check_PROGRAMS="$suites" TESTS="$suites"
done
make -C build/task175-on/test check \
  check_PROGRAMS=http3_network_owner TESTS=http3_network_owner
build/http3-venv/bin/python -m unittest discover -s test/integ \
  -p http3_websocket_client_test.py
python3 -m unittest discover -s test/integ \
  -p http3_websocket_gate_runner_test.py
python3 -m unittest discover -s test/integ -p http3_gate_runner_test.py
python3 scripts/run-v3-http3-websocket-gates.py \
  --build-dir build/task175-on --log-dir build/task175-websocket-evidence \
  --python "$PWD/build/http3-venv/bin/python"
```

The independent network gate is explicit because it needs TLS and external
client tooling. TLS-disabled native suites need no aioquic runtime. The wire
vectors run under ordinary Python; protocol unit tests require the pinned venv.
Use a fresh evidence directory for every network attempt.

## Evidence and local results

On Linux, the C++20 TLS-on/off builds and seven focused suites pass; the TLS-on
network owner suite also passes. The independent real UDP gate passes all eleven
scenarios. The client/wire suite has 12 tests, the WebSocket runner suite has 12
contract tests with corruption/process-failure subcases, and the existing
HTTP/3 runner suite has 21 tests. Changed C++ files pass cpplint; Python modules
compile and `git diff --check` passes. BSD, Windows and other nonlocal checks
remain unexecuted locally and belong to CI/the v3 PR per repository policy.

Each gate attempt retains commands, process exits, stdout/stderr, unvalidated
and successful receipts, three native qlogs, mode-0600 TLS key diagnostics,
actual datagrams in `packets.pcap`, and a derived `artifacts.json` credit witness.
The pcap's IPv4/UDP envelopes are synthesized at the application datagram
boundary; it is not a kernel packet capture. The manifest records tool/source
identity, source and fixture hashes, the complete working diff and its hash,
primary failure if any, and nonlocal-check status. Existing evidence is preserved.

The runner correlates exact connection/stream IDs with native request/response
headers, reset/STOP_SENDING/FIN evidence, TLS verification alerts, fixture
admission, backpressure, pending waits, resumed sends, close outcomes and handler
retirement. It rejects missing/duplicate cases or callbacks, wrong payloads,
forged/missing artifacts, early release, ambiguous commands and failed process
retirement. Synthetic process/receipt tests prove runner rejection behavior;
only the real UDP execution proves interoperability.
