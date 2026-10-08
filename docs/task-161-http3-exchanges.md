# TASK-161: HTTP/3 semantic exchanges

Implementation workspace: `.worktrees/TASK-161`, branch `task/TASK-161`,
base `v3` at `26df9b8ef44f52c46212b75fbd0922deb1b9ef98`.
This document records implementation checks; runner-owned validation,
finalization, commits and integration are separate.

## Composition and ownership

`http3_request_engine` composes the TASK-160 framing core with opened QUIC
streams, the authoritative flow ledger, reliable recovery storage, the route
registry and the existing `exchange` interface. The serialized connection owner
supplies these dependencies and keeps them alive through adapter destruction.
Connection identity and the available peer snapshot travel into each exchange.
There is no UDP listener, packet protection, TLS negotiation or public API change.

Initial HEADERS dispatch without waiting for DATA or FIN. Pseudo-fields, ordinary
fields, Host/authority, targets, Content-Length and trailers use shared semantic
validation without passing HPACK types through the HTTP/3 adapter. HTTP/2 retains
its Extended CONNECT behavior; HTTP/3 rejects CONNECT and `:protocol` in this slice.
Malformed semantic messages request a stream RESET with H3_MESSAGE_ERROR (0x10e).
Compression, framing and critical-stream failures retain the framing core's
connection error scope.

The body adapter reuses the existing fixed-capacity semantic rings. Their HTTP/2
window members do not participate in QUIC accounting. Admission clamps to the
configured H3 cap; bounded scratch holds borrowed DATA events through partial
copies. A fixed ordered receipt ledger records protocol extents and exact DATA
chunk extents. Application pulls alone advance body receipts, and protocol bytes
behind unread DATA remain uncredited. Trailers become visible only after a clean
QUIC FIN, successful parser completion and an application EOF observation.

Reliable response retention copies bounded HEADERS/DATA/trailers prefixes and
returns writer capacity before ACK. Successful retention advances the response
cursor; refusal preserves the cursor, body capacity and FIN. The adapter never
records actual stream emission. The owner must prepare a scheduled packet,
recheck scheduled emission, emit or abandon it, record the transport stream's
actual sent offsets, and commit recovery's flow accounting. Completion callbacks
remove only adapter-owned information IDs; the adapter never drains the shared
recovery completion queue. Local critical prefixes retain TASK-160 classification
and priority and are forwarded without FIN.

Owner actions expose STOP_SENDING and RESET_STREAM frames. RESET final size comes
from the QUIC stream's actual emitted high-water mark, never the response retention
cursor. Peer reset settlement is idempotent and releases only reset credit. A
committed response can abandon unread receive input with bounded storage and
STOP_SENDING; a peer RESET honoring that STOP preserves the valid response half.
Other peer resets, STOP_SENDING and connection failure invalidate handler frames
and cancel retained output. Clean request FIN leaves response production usable.
Stream facts remain bounded and prevent recreation of closed IDs.

Semantic heads, scratch, receipt descriptors, handler/stream metadata, ring peaks,
response encoding and trailer copies are reserved before allocation against the
QUIC storage lease. Semantic resource counters additionally track heads and body
rings. Cancellation frees partial response allocations before releasing their
reservations. The shared `request_handler_frames` helper preserves executor
affinity, queued witnesses and destruction after the outermost resume returns,
including inline cancellation and destruction from a handler.

## TDD evidence

The initial head and composition suites were compiled before the private H3
adapter existed. The RED logs identify the missing request-head/engine interfaces:
`/tmp/task161-red-headers.log`, `/tmp/task161-red-exchange.log`,
`/tmp/task161-red-streaming.log`, `/tmp/task161-red-cancellation.log`.

Behavioral RED/GREEN cycles also caught and repaired:

- Receive abandonment cancelling a valid 403 when the peer honored STOP_SENDING:
  `/tmp/task161-red-abandonment.log`.
- Response trailer limits being checked after `writer().finish()` reported
  success instead of before trailer copying: `/tmp/task161-red-trailer-cap.log`.
- Partial response storage surviving cancellation:
  `/tmp/task161-red-reset-charge.log`. The final assertion excludes the QUIC
  owner's independently retained reassembly descriptor storage.
- A pending borrowed DATA event hiding a transport RESET, and an automatically
  observed critical-stream RESET failing to surface its connection error:
  `/tmp/task161-red-auto-reset.log`.

- Local critical-stream terminal notifications bypassing the core or attempting
  receive reset settlement on a local send-only stream:
  `/tmp/task161-red-local-critical.log`.

The final focused suite has 29 scenarios and 433 checks across `http3_headers`,
`http3_exchange`, `http3_streaming` and `http3_cancellation`. All pass. Scenarios
cover shared real HTTP/1 poll/worker, HTTP/2 framing and H3/QUIC GET/POST handlers,
normalized/raw targets, repeated fields, incremental reads, collection, request
and response trailers, body/application/writer cancellation, sibling progress,
404/500 synthesis, unfinished response handling, exact receive credit, ring
wraparound, receipt saturation, zero DATA, 100-continue, response credit splitting,
recovery refusal, abandoned preparation, pre-ACK writer capacity, 62-bit SETTINGS,
critical prefix progress, inline destruction and reservation release.

## Reproducible local checks

Bootstrap and configure the ordinary C++20 build:

```sh
bash bootstrap
mkdir -p build/task161-off
cd build/task161-off
../../configure --disable-v3-tls --disable-examples \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-O1 -g'
make -C src -j2 libhttpserver_v3core.la
make -C test -j2 http3_headers http3_exchange http3_streaming http3_cancellation
make -C test check \
  check_PROGRAMS='http3_headers http3_exchange http3_streaming http3_cancellation' \
  TESTS='http3_headers http3_exchange http3_streaming http3_cancellation'
```

Actual compile commands include `-std=c++20`. The affected regression run passes
20 programs: `http3_frame`, `http3_settings`, `http3_connection`, `http2_headers`,
`http2_exchange`, `http2_streaming`, `http2_reset`, `quic_flow_control`,
`quic_flow_storage`, `quic_flow_recovery`, `quic_send_schedule`,
`qpack_field_section`, `http1_parser`, `http1_body_decoder`, `http1_body_source`,
`body_reader`, `response_writer`, `exchange_decisions`, `exchange_routing` and
`connection_engine`. The HTTP/1 engine regression links the same production v3core
with `LDADD='../src/libhttpserver_v3core.la'` and
`connection_engine_DEPENDENCIES='../src/libhttpserver_v3core.la'`; no mock or reduced
protocol implementation replaces it. Existing QUIC flow/recovery/scheduled-send
regressions cover ACK/loss retries and transport emission charging.

The sanitizer build uses the same configure options plus:

```sh
CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
LDFLAGS='-L/opt/homebrew/lib -fsanitize=address,undefined'
```

Run all four new suites plus `http2_headers`, `http2_exchange`, `http2_streaming`
and `http2_reset` with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
Production sources and tests both carry instrumentation. All eight pass.
Apple's local ASan runtime rejects `detect_leaks=1`; leak-sanitizer proof is not
claimed. Sanitizer logs are under `/tmp/task161-san-*`.

Changed C++ sources/headers/tests pass repository cpplint and production functions
pass lizard at CCN 10. `git diff --check` passes. Whole-repository complexity and
CPD checks still report exactly the committed baseline's 19 complexity warnings
and three duplicated runs, independently reproduced from a read-only HEAD archive
under `/tmp/task161-baseline-src`. No changed/new H3 or shared helper introduces a
finding. Those unrelated baseline violations are preserved. Full logs:
`/tmp/task161-clean-lint.log`, `/tmp/task161-changed-complexity.log`,
`/tmp/task161-baseline-complexity.log`, `/tmp/task161-baseline-duplication.log` and
`/tmp/task161-refactor-duplication.log`.

## Acceptance boundary

| Requirement | Local implementation evidence |
| --- | --- |
| REQ-007 | H3 request HEADERS/DATA/FIN reach real exchanges and produce QUIC responses |
| REQ-008 | Parked/saturated siblings, critical progress, exact receipts and scheduled credit |
| REQ-009 | Identical GET/POST handlers and cancellation over HTTP/1, HTTP/2 and H3 |
| REQ-021 | Bounded rings, admission, partial events, trailers and clean FIN |
| REQ-026 | Typed writer data/end/failure, reliable storage backpressure and cancellation |
| DR-V3-001/006 | Shared semantic API; private connection-owned protocol state and charged storage |

These are local deterministic composition and loopback checks. Independent H3
clients, UDP/TLS listener wiring, network interoperability, dynamic QPACK, Extended
CONNECT and staged GOAWAY remain later tasks. BSD, Windows and other nonlocal
platform execution is assigned to CI and the v3 PR by `AGENTS.md`; it was not run
here and does not block this local implementation phase.
