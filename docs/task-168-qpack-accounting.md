# TASK-168 QPACK accounting evidence

This implementation adds connection-owned QPACK accounting beneath semantic
requests (DR-V3-001 and DR-V3-006). Dynamic mode is configured through private
`http3_limits`; the existing zero-capacity mode remains the default. Integrating
these seams into request/response scheduling remains TASK-169 work.

## Ownership and admission

- Pool leases preserve both the protected data/critical subdivisions and the
  original ancestor budget. Decoder and negotiated encoder tables reserve
  `qpack_table_bytes` against that hierarchy. Each genuinely blocked request
  reserves one ancestor `blocked_headers` unit.
- HEADERS payloads remain in their existing frame parser. The connection counts
  their aggregate encoded length before payload allocation, independently of
  blocked count. Unblocking releases the count; event release, reset, and
  destruction release retained payload storage. No second encoded payload copy
  is introduced.
- A completed section stores its absolute Required Insert Count and Base.
  Each insertion/duplicate scans all eligible blocked streams before the next
  table mutation. Published events own decoded fields and remain immutable.
- Incoming instruction storage reserves a bounded critical envelope for wire
  bytes plus both decoded strings. Outgoing instruction scratch, pending output,
  section metadata/reference work, and returned encoded bytes are admitted
  separately. Arithmetic is checked before allocations and SETTINGS publication.
  A returned encoded section carries its pool lease and conservative storage
  reservation until destruction.
- Outstanding dynamic sections form a bounded connection FIFO ledger. ACK
  releases the oldest section on that stream and advances known-received count;
  cancellation releases all stream holds without advancing it. Insert-count
  increments advance accounting without releasing section references. Holds
  are destroyed before their encoder table.
- Peer blocked allowance counts streams. Encoding falls back to static-safe
  bytes when a new potentially blocked stream would exceed that allowance.
- Local decoder feedback acknowledges decoded dynamic sections, reports only
  insertion progress not already reported through acknowledgments, and cancels
  abandoned complete or partial undecoded sections. Pending bytes survive peek
  retries and partial advancement; bounded-output refusal fails the connection
  explicitly rather than dropping feedback.
- QUIC reservations commit already granted connection credit to valid opened
  local unidirectional stream ranges. They include any unsent gap before a
  pending range because QUIC charges high-water offsets. Ordinary sends exclude
  that credit; first transmission consumes it, retransmission does not. Range
  replacement, abandonment, FIN, and RESET return unused credit. Refusal leaves
  the ledger unchanged for retry after MAX_DATA/MAX_STREAM_DATA.

## TDD evidence

The initial connection baseline passed 18 tests / 296 checks. Regressions were
written before behavior changes:

- The ancestor-budget regression initially admitted 33 table units through an
  internal subdivision despite a 32-unit ancestor limit. The original hierarchy
  seam fixes that refusal and preserves envelope lifetime after pool destruction.
- Connection transcripts initially failed 21 checks for reordered blocked
  sections, fragmented/coalesced instructions, independent count/byte limits,
  FIN/RESET, and admitted nonzero SETTINGS. Those transcripts now pass.
- Outbound lifecycle and transport-credit tests introduced the missing private
  encoding, instruction, and critical reservation APIs, then verified FIFO ACK,
  shared holds, harmless cancellation, zero/one blocked allowance, and exact
  reserved-credit consumption.
- Fault injection initially reported encoder-stream/load errors instead of the
  required internal error on decoder-string/table-node allocation failures.
  Allocated codec/table seams now preserve `0x102`; legacy codec APIs retain
  their existing typed limit results.
- Additional runtime RED/GREEN regressions cover `0x107` instruction bounds,
  cancellation of partial HEADERS, preadmission of instruction strings and
  outbound wire scratch, and compressed-storage arithmetic overflow.

Focused builds use Littletest and the existing private v3-core library:

```sh
./bootstrap
mkdir -p build-task168
(cd build-task168 && ../configure CXX=clang++ --disable-examples --disable-v3-tls)
make -C build-task168 -j6
make -C build-task168/test -j6 \
  http3_qpack qpack_accounting qpack_dynamic_table qpack_instructions \
  qpack_dynamic_corpus qpack_field_section http3_frame http3_settings \
  http3_connection quic_flow_control quic_flow_storage quic_flow_recovery \
  http3_headers http3_exchange http3_streaming http3_cancellation
```

Run each executable under `build-task168/test/` with the same name. Literal wire
expectations cover section ACK (`80`), cancellation (`40`/`44`), insert increment
(`01`), and SETTINGS. Existing RFC 9204 corpus and codec suites remain registered.
The composition test saturates ordinary storage and retention, exhausts ordinary
connection credit, and still retains/emits both critical QPACK streams through
real recovery and flow ledgers.

## Local results and limits

Final local C++20 Autotools build and all 16 focused suites pass: 141 tests,
3,100 checks, zero failures, and zero skips. The new suites cover 25 tests and
294 checks. A direct Clang AddressSanitizer/UndefinedBehaviorSanitizer run of
`http3_qpack` also passes all 16 tests / 181 checks with no diagnostics, using
`ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer itself cannot run in this environment
because its process inspection is blocked under ptrace; no LSan result is claimed.
Changed-file cpplint and `git diff --check` pass. An additional whole-repository
file-size probe reports the unchanged baseline `src/detail/io_poll_backend.cpp`
(509 SLOC versus 500); this task does not modify that file. Changed modules are
below the per-file ceiling.

BSD, Windows, other nonlocal platforms, TLS-enabled integration, and network
request scheduling were not executed here. Nonlocal platform checks belong to
CI and the v3 PR under the project workflow policy. No validation/finalization,
commit, merge, or worktree lifecycle operation was performed by this executor.

## Development notes

Autotools is the project setup/build system; there is no package-manager install
step. Unit suites live in `test/unit/*_test.cpp`, use Littletest macros inside test
bodies, and reuse `http3_fixture.hpp` for bytes, frames, budgets, and ordered feed.
Private core implementation files live in `src/detail/`; private headers require
`HTTPSERVER_COMPILATION`. New sources/tests must be registered in the respective
`Makefile.am` files. Use Clang for this local C++20 build; the prior task documents
a GCC diagnostic in unrelated `server.hpp` code.

Avoid editing shared internal layouts during a parallel build: mixed object
layouts can appear current to incremental make. A clean source rebuild was used
here before compatibility checks. This runner worktree and its branch were reused
exactly; the external runner owns commits and cleanup. No separate persisted
agent-memory file was available; these development notes preserve the useful
setup and test facts for the next task.
