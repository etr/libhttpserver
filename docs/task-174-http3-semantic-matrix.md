# TASK-174 independent HTTP/3 semantic matrix

The explicit `--matrix task174` gate extends the existing smoke gate. Both
clients use their independent native QUIC/TLS/H3 stacks against the bounded
libhttpserver loopback UDP owner. The default smoke mode remains available.
The TASK-173 hq-interop transfers are not used as HTTP/3 semantic evidence.

## Observed behavior

The real network run `build/task174-semantic-07` passed both clients. A final
source replay evidence location is `build/task174-semantic-final`; focused local
checks are recorded below.
The finalized GOAWAY repair replay evidence location is
`build/task174-validation-goaway-final-iter2`.

| Category | aioquic 1.3.0 | quic-go v0.55.0 |
| --- | --- | --- |
| Verified handshake | QUIC v1, TLS 1.3, h3, no early data | Same |
| Concurrency | Held stream 8; health stream 12 completes before release | Same |
| Incremental binary body | Server reads six bytes before remaining body/FIN; exact 19-byte echo | Same |
| Response streaming/trailers | Multiple awaited writes; separate `x-reply: done` trailers and one terminal FIN | Same |
| Request trailers | `x-end: yes` visible after request EOF | Native public API lacks request-trailer sending; covered by aioquic and native regression |
| Cancellation | Native error 268; one server cancellation; sibling request succeeds on the same connection | Same |
| Slow producer | Six-byte prefix consumed; health completes on a sibling stream before upload resumes | Same |
| Slow consumer | 4096-byte stream credit exhausted; writer pending; health succeeds; granting credit resumes exact 512 KiB body and trailers | Same |
| GOAWAY | Received IDs `4611686018427387900`, `4`; accepted response completes; late stream 4 is rejected with 267; drain completes and connection closes with 256 | Same; received IDs decoded from captured packets |
| H1/H2 parity | Method, raw target/query, normalized route, Host/authority, ordered repeated fields, binary body, response fields/trailers and missing-route 404 match executed native H1/H2 | Same |

The upload is `6669727374007365636f6e64ff746869726400`. The large body is
`bytes(range(256)) * 2048`, SHA256
`33bc8aab40703678c3ebe94d2dd8f2afff285dd901f9234e841e4679f8204fd5`.
Parity normalizes field-name case and protocol framing, retaining duplicate
value order and trailer separation. All protocols execute the same semantic
handlers; H1 uses the real poll/worker connection owner and H2 uses the actual
HTTP/2 engine and HPACK codec.
The H1 observer retains the bounded socket transcript in `wire_hex` and splits
the framer's ordered fields at the actual initial-header block boundary. Field
names do not determine trailer placement. The `http3_semantic_h1` regression
rejects header-only `x-reply` as trailer evidence and accepts the actual chunk
trailer, including segmented input and repeated names across both blocks.

## Native observations and bounds

The fixture assigns monotonic event numbers and captures each stream identity
when its handler enters. Uploads wait for an actual server prefix-read event.
Cancellation requires exactly one matching server event. Targeted snapshot,
release and drain commands refer to the accepted connection/stream.

Slow consumers leave one awaited writer pending because transport credit and
bounded response storage are exhausted. The observed snapshot has five started
writes and four completed writes. The 4 KiB stream window leaves 16 MiB of
connection credit available to sibling/control streams. aioquic 1.3.0 normally
grows credit from received offsets; this test adapter suppresses only the
selected stream's `_write_stream_limits` during the withholding phase. It keeps
ACKs, other stream credit and the native H3 handler running. quic-go uses its
native 4 KiB receive window without reading the selected response body.
Both qlogs must independently show credit exhaustion, sibling FIN, subsequent
MAX_STREAM_DATA and resumed response FIN in that order.

The aioquic observer records actual GOAWAY control payloads while retaining its
native handler. quic-go's public API has no received-ID observer, so the gate
uses tshark's decrypted incoming H3 frame payloads. The late request stream is
opened before drain; its headers are sent after both received GOAWAY stages.
A server `drain_started` event alone cannot satisfy the gate. The drain ticket
wait runs outside the serialized UDP owner with a five-second deadline.
The runner refreshes the live capture snapshot only while decoding a valid
incomplete received-stage prefix. It acknowledges the client only after both
actual incoming IDs match the initial maximum and accepted-stream cutoff.
Malformed rows/integers, incorrect stages, capture failures and decoder failures
fail immediately. Refreshes and decoder execution share the original ten-second
witness budget, clipped to the earlier case and five-second drain deadlines;
an absent stage at that deadline fails closed. The final decoder metadata
retains the snapshot hash and observation-attempt count.

Connections retain their existing 15-second lifetime. The fixture caps four
connections, 16 active semantic requests per connection, executor turns, 64 KiB
uploads, 512 KiB response accumulation, 16 MiB capture bytes and diagnostic
output. Evidence directories are private; TLS secrets have mode 0600.
`packets.pcap` contains real application-boundary datagrams with synthesized
IPv4/UDP envelopes, rather than an interface capture.

## Provisioning and reproduction

Local tools: Clang 18 (C++20 in actual compiler output), Python 3.12, Go 1.26.9,
and tshark 4.6.4. The clients retain the committed Python requirements and
Go module/checksum pins. The Go client was rebuilt from this worktree.
The official linux-amd64 Go archive SHA256 is
`42d158b4d8f7b61ac0a830567c940a86098fb7aac52e467a5ebec03ef5cc2f8d`.
Python dependencies and Go caches live in ignored `build/`. The decoder and
its runtime libraries were extracted, read-only, from the existing
`task163-runner-tools:local` image
`sha256:354244ace64ea691e55628b3aca83c420acca54c65a4edc94130d61681be6266`.
`build/tooling/tshark` invokes the extracted ELF loader and library path.
The OpenSSL 3.5.9 provider is reused read-only from the repository's tooling.

```sh
python3 -m venv build/http3-venv
build/http3-venv/bin/python -m pip install -r test/integ/http3-client-requirements.txt
GOPATH="$PWD/build/tooling/gopath" GOCACHE="$PWD/build/tooling/gocache" \
  build/tooling/go/bin/go build -C test/integ/http3-client-go \
  -o ../../../build/http3-client-go .
bash bootstrap
mkdir -p build/task174-on
cd build/task174-on
../../configure --enable-v3-tls --disable-examples CC=clang CXX=clang++ \
  CXXFLAGS='-O1 -g' \
  V3_TLS_CFLAGS='-I/home/etr/progs/libhttpserver/build/tooling/provider/include' \
  V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'
make -C src -j4 libhttpserver_v3core.la
make -C test -j4 http3_udp_fixture http3_semantic_parity_fixture \
  http3_semantic_h1 \
  http3_network_owner http3_exchange http3_streaming http3_cancellation \
  http3_qpack_scheduling http3_drain
cd ../..
python3 -m unittest discover -s test/integ -p http3_gate_runner_test.py
build/task174-on/test/http3_semantic_h1
python3 scripts/run-v3-http3-gates.py --matrix task174 \
  --build-dir build/task174-on --log-dir build/task174-semantic-final \
  --python "$PWD/build/http3-venv/bin/python" \
  --go-client build/http3-client-go --tshark build/tooling/tshark
```

Use a fresh evidence directory on each invocation. Existing evidence is
preserved. Each attempt retains stdout/stderr, exits/commands, captures, qlogs,
TLS diagnostics and the primary failure. Completed client receipts remain when
a later client fails. The manifest hashes the source inputs, Go module files,
fixture/client binaries and the complete working diff, including new files.
The final capture-derived GOAWAY TSV/command and per-client flow witness are
retained beside the raw diagnostics. Fake-process contract tests establish
runner rejection behavior; they are not interoperability evidence.

## Failed attempts and fixes

- `task174-semantic-01` and diagnostic probes `-02` through `-04`: a large
  response could stall. The fixture sampled `now` before applying a successful
  send completion, which committed a later emission timestamp. The next
  packet preparation therefore failed monotonic-time admission. Sampling
  after completion fixes the fixture; no production protocol change was needed.
  Diagnostic probes that forced extra owner turns are retained as failed
  investigation output, and are not accepted matrix evidence.
- `task174-semantic-05`: an adapter selector error bypassed the wrong-CA
  observer. Corrected the selector and retained the failing receipt.
- `task174-semantic-06`: Go's generic JSON decoding rounded the 62-bit received
  GOAWAY ID. The new failing Go regression reproduces this precisely; using
  `json.Decoder.UseNumber` preserves the actual wire value.
- `task174-semantic-07`: both complete matrices passed, including executed
  H1/H2 parity. The added native qlog artifact checks also passed on this run.
- `task174-validation-repaired-ec708b4a` is retained as a failed attempt:
  its one-shot GOAWAY snapshot ended at packet 1991, after the first stage
  at packet 1989 but before the second stage at packet 1994. The final capture
  and native qlog establish that both stages arrived. Bounded snapshot refresh
  fixes this observation race without changing protocol behavior or extending
  any deadline. `task174-validation-goaway-iter2` passed both pinned client
  matrices and executed H1/H2 parity after the synchronization repair.

BSD, Windows and other nonlocal checks are deferred to CI/the v3 PR per the
repository's task workflow. No nonlocal passing evidence is claimed.

## Local verification

- TLS-on C++20 build: six focused suites, 101 tests, zero failures; native H1/H2 parity fixture passed.
- TLS-off C++20 build: five focused suites, 68 tests, zero failures; native H1/H2 parity fixture passed.
- H1 wire-placement repair: TLS-on/off C++20 regression passed all four tests
  and 29 checks; each native parity replay retained the real H1 transcript.
- Python runner contracts: 21 tests passed, including growing-capture receipt
  synchronization, bounded absent-stage failure, strict invalid/tool/capture
  rejection, subprocess-budget clipping and rejection of evidence arriving
  after the deadline, alongside existing receipt/flow/primary-failure checks.
- Go adapter regression and `go vet`: passed. Python compilation and changed-file cpplint: passed.
- `make check-local`: passed; its Doxygen subgate explicitly skipped because Doxygen is unavailable locally and remains enforced in CI.
- `make distdir`: passed; the new fixture, shared route header, Python adapter, Go implementation/test and documentation are included.
- `git diff --check`: passed. The default smoke gate is retained alongside the semantic replay.

Build/test outputs are retained under `build/tooling/`. Task tracking remains
In Progress for the coordinator-owned validation/finalization phases.
