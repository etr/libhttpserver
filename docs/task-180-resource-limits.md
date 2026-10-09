# TASK-180 hierarchical limits and slow-peer verification

Verification uses actual private HTTP/1, HTTP/2 and HTTP/3 owners, real HTTP/1
sockets, TLS/hyper-h2 and QUIC v1/TLS/aioquic peers. Public TLS/H2/H3 listener
composition remains the release gap recorded by TASK-179; this task does not
implement it or claim public listener coverage. Nonlocal BSD, Windows and macOS
checks remain assigned to CI and the v3 PR under AGENTS.md.

## Owner accounting repairs

HTTP/1 now charges retained request heads/fields, active exchanges, early input,
body segments and trailers, serialized output, and WebSocket queue/message
reservations through its real budget. Admission precedes retained copies. A
move-only counter handle bounds bookkeeping even for single-byte fragments.
Read/output retirement and final loop retirement return capacity. WebSocket
reservations include transport tails, codec copies, message descriptors and
control/output headroom. Existing per-queue caps still apply independently.

HTTP/2 and HTTP/3 protocol connection owners reserve actual connection seats.
QUIC's data/critical subdivisions now forward every semantic quota to the real
ancestors, while their already prepaid storage envelope stops locally: one
4 MiB data + 1 MiB critical envelope, rather than charging it twice. Critical
storage remains unavailable to ordinary data. HTTP/3 retires request stream
seats once application/information work retires on cancellation or reliable
FIN retention; bounded, separately charged transport tombstones remain and
still reject recreated IDs. Wire emission and ACK retention remain distinct.

Two H1 fixtures that previously equated a 2048-byte per-queue allowance with
aggregate storage now explicitly retain that queue cap and provide 65536 bytes
for the simultaneous early-input, refeed/staging copies and descriptors. Their
loss/order assertions are preserved.

## Resource and cancellation matrix

| Engine | Actual-owner witnesses | Existing supporting focused suites |
| --- | --- | --- |
| H1.0/H1.1 | Fragmented exact head byte/field caps, parent/child/sibling refusal and reuse; live head/exchange charges; aggregate connection seats; early and admitted input; shared body segments, trailers and output retirement | http1_parser, connection_engine, http1_body_mode/decoder/source, http1_response_outbox |
| WS/H1 | Actual upgrade queue/message envelopes, exactly-once close and terminal release; socket input/output saturation | http1_websocket_upgrade, websocket_codec/session, websocket_drain/race |
| H2 | Connection seat rejection/reuse, shared WS ancestors for headers/body/output/streams/message limits; paused upload/response with real TLS, reset and sibling 204/PING ACK | http2_exchange/headers/streaming/rate_budget/websocket_flow/lifecycle |
| H3 | Ancestor semantic headers and connection seats; single QUIC envelope; reset releases request seat for replacement; shared WS ancestors; actual UDP upload/response credit stalls, reset and sibling 204/ACK/PING | http3_frame/connection/headers/streaming/cancellation/websocket_flow/lifecycle/qpack_scheduling, quic_flow_storage, http3_network_owner |

The task-specific `resource_limits` suite exercises real owners below shared
and child scopes. Repeated refused attempts preserve usage; destruction,
consumption and real resets restore admission. The H2/H3 WS sibling cases sum
actual child usage for connection, stream, header bytes/fields, input/output and
message resources against the parent. Child-only refusal is tested while the
parent still has free capacity. These are engine-owned reservations, without
client-side quota guards substituting for production admission.

Existing focused cases provide compressed vs expanded HPACK/QPACK limits,
stream-scoped refusal preserving compression state, byte/count/empty-message
queues, cumulative/declared fragmented message limits, route tightening,
partial borrowed output cancellation, incremental body credit, bounded
collection/trailers, QPACK cancellation and independently reserved critical
progress. `server_options_validate` and `native_server` preserve validation
before binding, including zero/above-ceiling and incompatible transports.

## Normal allocator memory and CPU

H1 `native_protocol_memory` applies reusable-buffer 1 MiB and 8 MiB attempted
socket/producer pressure, measures current RSS, and checks actual locked queue
snapshots. Upload and response peers resume and retire; WS input/output checks
also cover retirement and close. Each of its four saturated cases now has
three equal one-second CPU observation windows with an idle peer/observer.
`CLOCK_PROCESS_CPUTIME_ID` includes the actual server workers and sleeping
observer in that executable; it is not an independent server-only measurement.

`scripts/lib/resource_limits_probe.py` measures separate running server
processes through Linux `/proc/<pid>/stat` (user + system CPU ticks) and `statm`
(current RSS, not historical maximum). Its independent clients retain bounded
per-stream responses and bounded transcripts. It performs three output
saturate/reset/refill cycles and three upload saturate/reset/refill cycles for
both TLS H2 and protected UDP H3, plus ordered response resumption. Production
receive credit bounds client uploads; no illegal excess DATA is sent to defeat
flow control. Additional actual PING pressure uses 64 and 512 control frames;
H2 stays below the production control-rate gate and checks every PING ACK. H3
keeps ACK/PING active and completes a PING future. These are control-frame
pressure windows, not claimed 1/8 MiB network data attempts. The H1 probe owns
those attempted-byte measurements. Each stalled window permits at most 20%
of one CPU core and each warm RSS comparison permits at most 1 MiB growth.
A 90-second top-level deadline and bounded fixture shutdown enforce cleanup.

The final network receipt is `build/task180-probe-08/receipt.json`, with bounded
server logs, ephemeral credentials and a datagram capture in the same directory.
The H3 probe stays within the existing owner's initial sixteen bidirectional
transport streams. It does not claim transport MAX_STREAMS replenishment.

| Normal network witness | Saturated actual usage | Post-reset baseline | Ordered resume |
| --- | --- | --- | --- |
| H2 output | response 20608 B; header 540 B; body 18472 B; 1 request stream | response 4096 B; body 16512 B parser reserve; headers 0; streams 0 | 131072 exact `L` bytes |
| H2 upload | body 84135 B; header 720 B; accepted stream input 16384 B and zero remaining stream credit | body 16512 B; headers 0; streams 0 | reset/readmission plus existing upload-flow transfer |
| H3 output | response 16512 B; body 16512 B; header 900 B; 4 peer streams | response/body/header 0; 3 fixed peer critical streams; 5242880 B prepaid QUIC envelope | 524288 exact pattern bytes, one FIN |
| H3 upload | body 16512 B; header 1200 B; 131072 B supplied; sender offset and credit both 65573 B, with unsent data parked | body/header 0; 3 fixed peer critical streams; same envelope | reset/readmission with sibling 204 |

All three output and upload cycles returned to these exact semantic baselines.
The QUIC envelope survives while the owner exists and is released by its
storage lease lifetime; queue destruction is not incorrectly asserted to free
closing/CID state immediately. Actual-owner unit tests verify full destruction
release and late/idempotent terminal behavior.

The six H2 one-second CPU samples were 0, 0, 0, 0, 0, .02 seconds;
RSS stayed at 9699328 B throughout. H3 measured CPU was zero ticks in all six
windows (10 ms measurement resolution); its warm RSS range was 11272192–11534336 B, a 262144 B increase, within
1 MiB. Every sample retains CPU/wall/RSS values in the receipt. Pressure CPU,
actual accepted response bytes and control counts are recorded separately.
The TLS-off H1 log is `build/task180-off/test/native_protocol_memory.log`: its
four queue snapshots were 4096/4096, 1024/1024, 2122/2122 and 462/462 bytes;
RSS was unchanged within each pressure pair. CPU samples were below .001
seconds per one-second idle window.

## Reproduction and local evidence

Use `./bootstrap`, then fresh VPATH directories. Normal configurations use
`CC=clang CXX=clang++`, `--disable-examples`, C++20 `-O1 -g`, and either
`--disable-v3-tls` or `--enable-v3-tls`. TLS-on uses the existing OpenSSL 3.5.9
QUIC-capable provider at `/home/etr/progs/libhttpserver/build/tooling/provider`
with explicit include/library flags. The compiler is Clang 18; client pins are
hyper-h2 4.4.1 and aioquic 1.3.0. The pinned environment is
`build/task180-clients`, provisioned explicitly rather than inside a gate.

```sh
build/task180-clients/bin/python scripts/lib/resource_limits_probe.py \
  --h2-fixture build/task180-on/test/http2_tls_fixture \
  --h3-fixture build/task180-on/test/http3_udp_fixture \
  --output build/task180-probe-fresh
```

Build all `src` libraries before tests: some H1/public tests link the legacy
public shared library embedding v3core; rebuilding only the static core leaves
stale behavior. Build focused executables explicitly, then run Automake
`check-TESTS check_PROGRAMS= TESTS='<selection>'`. The dependency-free Python
receipt contract is also registered with Automake and checks CPU spin and
RSS-growth rejection.

RED logs are retained under `build/task180-off/test/resource-limits-red*.log`
and `resource-limits-trailers-red2.log`: missing H1 head/body/output/early/WS
charges, H2/H3 seats, missing H3 ancestry and unreleased cancelled stream seats
were observed before repairs. A long H1 response initially exhausted historical
output charges after transmission; retirement now permits all 128 refill
turns. The trailer RED case refused neither shared fields nor their storage.
Network probe RED demonstrated unavailable real-owner budget snapshots.
The first control-pressure attempt correctly received H2 overload GOAWAY;
subsequent probes retain the production rate limit and pace legal pressure.
A longer first H3 probe exhausted its fixture's fixed transport stream count;
the bounded final scenario respects that existing owner contract.

An additional H1 tail regression observed retained heap capacity and a stale
144-byte reservation after the consumer drained all pending input. The owner
now frees empty tail allocations before returning capacity and reconciles
charges after refeed; partially consumed tails retain their allocation charge.
Its RED receipt is `resource-limits-tail-red.log`.

Sanitized configurations add `-fno-omit-frame-pointer`,
`-fsanitize=address,undefined -fno-sanitize-recover=undefined`, matching linker
flags, `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`.
LeakSanitizer cannot run under this environment's ptrace supervision; the
provider archive is separately uninstrumented. Sanitizer behavioral results
are kept separate from normal RSS/CPU measurements.

The supported TSan lane uses `-fsanitize=thread` with the existing
`test/tsan.supp` limited to documented libstdc++ ctype-cache races. Its global
allocator owns operator new/delete. The existing WS drain race fixture now
uses the repository's sanitizer-owned allocator convention: all actual race
cases execute, while its non-race allocation fault injection reports one
explicit internal case skip under TSan. That fault-injection case executes in
normal and ASan/UBSan lanes; it is not reported as TSan proof. The original
linker conflict and repaired build are retained in TSan build logs.

Final current-source local receipts:

| Lane | Passed suites | Receipt |
| --- | --- | --- |
| Normal TLS-off | 33/33 | `build/task180-off/check-complete.log` |
| Normal TLS-on | 34/34 | `build/task180-on/check-tail-current.log` |
| ASan/nonrecovering UBSan TLS-off | 32/32 | `build/task180-asan-off/check-tail-current.log` |
| ASan/nonrecovering UBSan TLS-on | 33/33 | `build/task180-asan-on/check-tail-current.log` |
| TSan affected H1/race owners | 5/5; one non-race fault-injection case explicitly skipped | `build/task180-tsan-off/check-tail.log` |

The final resource-owner suite passes 19 cases and 762 checks in all five lanes; final task-local additions
also prove H1 WebSocket child refusal with free parent capacity, parent
saturation across sibling connections, and reuse after real socket closure.
The final added case was rerun in all five lanes after the full focused sets
above passed. These combined results verify 502 C++ cases in TLS-off and 535
in TLS-on, plus the Python contract and normal allocator executable.
Their five-lane receipts are `build/task180-<lane>/check-resource-final.log`. Normal and
ASan/UBSan selected suites have no skips or errors; the independent Python
contract adds three passing receipt tests. Normal lanes additionally run the
RSS/CPU executable; sanitizer lanes verify behavior without allocator plateau
claims. TLS-on also runs the actual QUIC/TLS network-owner suite.
An intermediate ASan-on run picked up the new tail regression against its
previous static core and failed that case; the final receipt above rebuilt all
source libraries and passes it. The stronger H3 upload witness initially found the supplied 64 KiB had all
fit the current credit. Its final probe supplies a fixed 128 KiB buffer and
asserts both an exhausted actual send window and remaining unsent data.
A prior compile overlapped a test-file write;
its failed compiler log is retained and the final builds used the stable source.

 No tests were
substituted for required network evidence, no public metrics/API were added,
and the protocol/control admission guards were preserved.

The unchanged file-size gate still reports `src/detail/io_poll_backend.cpp`
509 SLOC against 500. Changed sources remain below the gate; the threshold was
not relaxed. Changed C++ paths pass cpplint, Python compiles and `git diff
--check` passes. Runner-owned publication and task completion follow this
implementation handoff; the task card remains In Progress.
