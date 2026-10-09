# TASK-169: Dynamic QPACK request scheduling

The private HTTP/3 adapter now uses its connection encoder for informational
headers, final response headers, and trailers. Compression state stays below
`exchange`, with serialized processing and hierarchical admission as required
by DR-V3-001 and DR-V3-006.

## Encoding and section ownership

The encoder chooses capacity within the locally admitted maximum and the peer's
SETTINGS. The Required Insert Count modulo still uses the peer's advertised
maximum. Before SETTINGS, responses remain static. Zero admitted capacity avoids
optional instruction storage. A zero peer blocked-stream allowance permits
references only after insert receipts establish that they cannot block.

The initial policy caches custom names with empty values, reuses existing names,
and skips static-table names and names beyond instruction bounds. Regular response
values remain never-indexed literals. Insertion refusal leaves static-safe encoding
available; mandatory feedback exhaustion retains its existing connection error.
Field views, frame construction, encoded bytes, and reference metadata are admitted
before allocation, and the encoded-byte lease survives frame construction.

New response sections receive bounded provisional identities. Queue construction
failure and local cancellation release unpublished holds. The first successful
recovery retention publishes the section, including partial retention. Published
holds survive send cancellation until FIFO section ACKs, decoder cancellation, or
connection shutdown. A peer cannot acknowledge an unpublished section. Receive
abandonment and its matching RESET preserve the response half.

## Receive and critical-stream scheduling

Blocked HEADERS keep QUIC FIN pending. Encoder instructions decode eligible
sections before a subsequent mutation can evict their entries, and adapter turns
service ready siblings without another request datagram. Output turns also progress
buffered receive continuations. Ordinary continuations rotate after actual progress;
control and both QPACK roles have separate bounded turn allowances. A separate
classification allowance admits new critical roles even under sustained control
input.

DATA continues borrowing immutable scratch until delivery finishes. Protocol
receipts never credit framing beyond unread DATA. Trailers become visible after
clean receive completion and application EOF observation, including a paused body
consumer.

Each local critical role owns one recovery retention opportunity. A three-element
role index avoids rescanning every ordinary record for each credit check. Critical
transport reservations cover admitted prefixes and the unsent retained high-water
gap, including pending instructions while the preceding prefix is retained.
Successful critical service rotates when aggregate credit cannot admit all
roles. Ordinary output rechecks reservations after trailer preparation. Retention refusals
for byte or storage capacity retry successively smaller prefixes, with a logarithmic
retry bound; FIN and cursors advance only for the admitted range.

Recovery retention advances the QPACK output cursor; packet preparation, refusal,
and abandonment do not advance it again. The owner still schedules, rechecks,
emits, records stream offsets, and commits recovery accounting. First emission
consumes reserved credit; retransmission uses the original credit. Disconnect and
connection failure cancel owned recovery information, abandon critical reservations,
and release blocked payloads, decoded events, instruction scratch, tables, and
section holds in dependency order. Late transport notifications are inert.

## Regression evidence

The first scheduling run failed because response sections were static-only,
encoder input left ready siblings parked, ordinary output had no protected critical
credit, and disconnect retained blocked/table accounting. Subsequent RED runs caught:

- Provisional holds surviving local cancellation.
- Ordinary work exhausting the shared encoder instruction turn allowance.
- A busy ordinary record repeatedly taking the next turn.
- Acceptance of ACKs for unpublished response sections.
- Output storage readmission and late notifications after disconnect.
- An unnecessary 8 KiB instruction-output reservation with zero admitted capacity.
- Pending encoder instructions losing their protected credit while their stream
  type prefix was still retained.
- New critical stream types sharing an exhausted control allowance.
- Encoder and response ranges larger than the available recovery byte allowance.

The new deterministic suite covers fragmented encoder input, different Required
Insert Counts, pending FIN, paused DATA, blocked trailers, mutation after decoding,
ordered repeated fields, dynamic responses with delayed encoder delivery, partial
critical writes, MAX_DATA/MAX_STREAM_DATA retries, packet abandonment, provisional
and published cancellation, and terminal cleanup. Its combined transcript adds
independent ordinary record/byte/storage saturation, critical stream emission,
loss and retransmission, exact body credit, response-driven receive abandonment,
FIFO response/trailer ACKs, and one cancellation instruction for a reset stream.
This composition provides local evidence for PRD-V3N-REQ-008, REQ-017, and REQ-027.

## Local verification

On 2026-10-08, both lanes rebuilt production objects from a clean directory:

- Clang C++20: `./bootstrap`, out-of-tree `configure CXX=clang++
  --disable-examples --disable-v3-tls`, and `make -j6` passed.
- All 19 focused executables passed: 205 tests, 4,184 checks, zero failures,
  zero skips. These cover HTTP/3 request/exchange/frame/SETTINGS semantics,
  QPACK primitives/dynamic accounting, and QUIC storage/flow/recovery/scheduling.
- The new scheduling executable passed 19 tests and 378 checks, including the
  combined saturation/reordering/cancellation/loss transcript.
- Clang ASan/UBSan passed scheduling, HTTP/3 QPACK, QPACK accounting,
  cancellation, and streaming: 73 tests and 1,213 checks, zero failures or skips.
  Undefined behavior used `UBSAN_OPTIONS=halt_on_error=1`.
- Changed-file cpplint and `git diff --check` passed. New/modified task functions
  passed CCN 10; baseline comparisons and the file-size exception are below.

Detailed logs remain in ignored `build-task169/` and `build-task169-san/` directories,
including `focused-summary.txt`, individual `*-final.log` files, RED/GREEN receipts,
and clean build logs.

LeakSanitizer cannot run under the environment's ptrace supervision. The sanitizer
configure probe reproduced its fatal diagnostic; the ASan/UBSan lane disables leak
collection with `ASAN_OPTIONS=detect_leaks=0` and leaves address/undefined checks on.

Changed-file cpplint is required to pass. Lizard comparison against commit
`656d116d` shows no new CCN-over-10 functions: the previous QPACK input (22),
encoder instruction queue (11), and section encoding (17) were split into bounded
helpers. Five unchanged baseline offenders remain in connection validation,
field decoding, terminal handling, SETTINGS parsing, and generic table acquisition.
The file-size gate reports the unchanged `io_poll_backend.cpp` at 509 SLOC; changed
production modules remain below 500 SLOC.

Native TLS/network execution is unavailable locally: the required OpenSSL 3.5
provider is absent from pkg-config. The existing network fixture therefore remains
unchanged. Independent client probes and BSD/Windows/nonlocal platform checks
belong to CI and the v3 PR; they were not executed here. Coordinator validation,
runner-owned commits, merge, and cleanup remain separate phases. Staged GOAWAY and
drain admission policy remain TASK-170.
