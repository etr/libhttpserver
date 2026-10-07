# TASK-140 implementation evidence

Registered worktree: `/Users/etr/progs/libhttpserver/.worktrees/TASK-140`.
Branch: `task/TASK-140`; base: `v3`, starting HEAD
`9bc49b9fc6f6cb583f699c3748dcbc671cf634cd`.
The supplied existing worktree was verified with branch, status, log and
`git worktree list`; no worktree lifecycle, staging or commit was performed.
Task and index remain In Progress; the implementation action items are checked.
The runner owns validation, final status, commits, integration and cleanup.

## Implemented boundary

`http2_request_engine` composes the existing private connection, its shared
HPACK state and the shared `run_route` exchange runner. It owns one bounded
connection-wide HEADERS/CONTINUATION block, strips HEADERS padding/priority
prefixes and preserves the initial END_STREAM through subsequent fragments.
The existing parser remains the continuation-order authority. Completed blocks
are decoded exactly once, including refused, closed and priority-invalid streams,
so semantic/stream rejection does not lose connection compression state.

The private converter enforces pseudo-field ordering, uniqueness and request
vocabulary; method/scheme/path requirements; authority/Host consistency;
lowercase token names; valid values; forbidden connection fields; TE trailers;
and content lengths consistent with an empty body. It preserves ordinary
occurrence order and exact path bytes, then derives the route using the HTTP/1
normalizer. URI schemes use URI syntax rather than an http/https-only whitelist.
These rules follow [RFC 9113 section 8](https://www.rfc-editor.org/rfc/rfc9113.html#section-8).

Each accepted stream owns the head, sink, exchange, reservations and route task.
The connection-owner executor serializes handler resumptions with engine calls.
Stream 1 can suspend while stream 3 finishes. Owned coroutine frames remain
alive at final suspend until reaping and are invalidated on destruction/failure;
queued resumptions cannot access destroyed exchanges. Sink-triggered connection
failure defers frame destruction until the executing resume has returned.
The existing route runner supplies lookup, 404/500 synthesis and containment.

Response decisions queue semantic fields under resource admission. Encoding
happens in transmission order through the connection encoder after output
capacity is reserved. Status precedes lowercase ordinary fields; bodyless
HEADERS carry END_STREAM and large blocks use CONTINUATION at the peer frame
limit. Literal response fields use without-indexing. A peer header-list refusal
resets that stream before touching the encoder; valid siblings still respond.
Started field blocks and borrowed spans survive partial advancement and
connection failure, finishing before GOAWAY. GOAWAY reports the highest
stream dispatched to an exchange. Semantic errors produce RST_STREAM;
compression/continuation failures terminate the connection.

Compressed storage is charged against body buffering; decoding/conversion and
retained heads use header-byte/field resources with conservative copy allowance;
stream objects and output queues use their existing hierarchical resources.
Temporary block storage is freed after decode. Tests cover zero-capacity child
admission rollback, suspended/queued task destruction, and reservation release.

Non-END_STREAM request heads and CONNECT are refused at this seam. Streaming
response writes/finish return a typed unsupported failure and reset the stream.
DATA delivery, flow control, broader reset/rate machinery and WebSocket upgrade
support remain subsequent-task work. All new headers and sources are internal.
The public listener still constructs HTTP/1: the TLS test proves private
negotiated-h2 adapter composition, not public-listener HTTP/2 service.

## TDD receipts

Behavioral RED logs are local `/private/tmp/task140-*.log` files:

- `headers-red`: missing route calls, missing decodable response and missing
  compression failure; 3 tests, 6 failed checks against the empty engine seam.
- `exchange-red`: absent concurrent dispatch, synthesized responses, stream
  admission, header limits and fragmented output; 5 tests, 10 failed checks.
- `exchange-edge-red`: peer header-list refusal poisoned the encoder, and
  ASan detected destruction of an executing handler during output-budget refusal.
  The final implementation rejects before encode and defers destruction.
- `priority-red`: priority-invalid HEADERS prevented the following stream from
  progressing; now the complete block is decoded and the event is released.
- `scheme-red`: HTTPS and two other valid URI schemes incorrectly failed routing;
  6 failed checks, followed by 5 tests / 1,964 successful checks.
- `terminal-red`: a positive GET response content length was emitted with no body,
  and GOAWAY incorrectly reported stream zero after route dispatch; 3 failed
  checks, followed by passing focused exchange/connection/conformance suites.
- `metadata-red`: HEAD and 304 response lengths were incorrectly treated as body
  bytes; 2 failed checks. The final 13-test exchange suite also verifies that
  conflicting lengths are rejected, with 175 successful checks.

Bring-up linker errors are not behavioral RED. TLS exchange composition was
added after the engine unit behavior was GREEN, using the real existing adapter
and independent OpenSSL peer fixtures. It decrypts five-byte request segments,
encrypts seven-byte output segments, decodes responses and proves stream 3/204
arrives while stream 1 is parked, then stream 1/201 arrives after resumption.

## Local commands and results

macOS arm64, Apple clang C++20. The existing provider was rechecked directly:
`/private/tmp/task129-provider/install/bin/openssl version` reports OpenSSL 3.5.9.
Task-local out-of-source builds use the TASK-139 provider recipe:

```sh
./bootstrap
mkdir -p build-off build-on
cd build-off
../configure --disable-v3-tls CPPFLAGS=-I/opt/homebrew/include \
  LDFLAGS=-L/opt/homebrew/lib CXXFLAGS='-std=c++20 -O0 -g'
make -j2
cd ../build-on
../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -j2
cd ..
```

TLS-off focused gate:

```sh
make -C build-off/test -j2 http2_headers http2_exchange http2_frame \
  http2_settings http2_connection http2_conformance hpack_connection \
  hpack_field_section exchange_routing http1_parser
make -C build-off/test -j1 check-TESTS \
  check_PROGRAMS='http2_headers http2_exchange http2_frame http2_settings http2_connection http2_conformance hpack_connection hpack_field_section exchange_routing http1_parser' \
  TESTS='http2_headers http2_exchange http2_frame http2_settings http2_connection http2_conformance hpack_connection hpack_field_section exchange_routing http1_parser'
make -C build-off check-v3-native-linkage check-local
```

TLS-on repeats that gate in `build-on`, adding `http2_tls_boundary`,
`tls_selection`, `tls_credentials` and `tls_io` to the build and both test lists.
Both full C++20 builds and native-linkage/check-local gates pass.
TLS-off: 10/10 executables, 142 tests / 19,812 checks.
TLS-on: 14/14 executables, 166 tests / 20,614 checks.
After the final output-loop/copy-allowance refactor, affected headers/exchange
suites were repeated in both modes and the TLS boundary suite was repeated on.
The new engine suites contain 18 tests / 2,139 checks with no failures.

Focused ASan/UBSan recompiles frame, connection, request-head and request-engine
sources with `-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined`, then
links/runs the two new suites with the same flags and the existing v3core
archive for the shared WebSocket ABI. Final results: 18 tests / 2,139 checks,
no sanitizer diagnostic. Artifacts are `/private/tmp/task140-sanitizers/`.
This is task-local sanitizer proof, not whole-library sanitizer coverage.

Changed-source cpplint, changed-source Lizard at CCN 10, the repository file-size
gate and `git diff --check` pass. Additional whole-source lint probes found
existing baseline findings, with no TASK-140 source in their diagnostics:

- Global complexity: `dispatch_request` (11), `valid_peer_pattern` (15),
  `pollsys::accept_one` (11). Each was reproduced from `git show HEAD:<file>`.
- Global duplication at 100 tokens: existing body-reader/response-writer wait
  nodes, and the existing task value/void ownership methods.

The six affected baseline files are unchanged from starting HEAD. These global
CI-lint findings need repository-owner triage in the v3 PR; no unrelated repair
was added to TASK-140. Logs: `task140-complexity-baseline.log` and
`task140-duplication.log` under `/private/tmp`.

BSD, Windows and other nonlocal platform checks were not executed. They remain
CI/v3 PR-owned under AGENTS.md and do not block the local task gate. Public
listener wiring, independent external HTTP/2 client conformance and the full
repository test-suite gate are not claimed by these focused implementation
receipts; broader validation remains runner-owned.

## Validation repair receipts (iteration 1)

The authorized repair envelope covered six findings from performance,
test-quality and specification alignment review. No staging, commit, merge,
listener wiring or successor-task implementation was performed.

Effective `:authority` now appears as a synthetic `host` occurrence when the
received ordinary fields contain no Host. Received ordinary-field order is
preserved, and an explicit Host still must agree with authority. The shared
route parity regression chooses its status using Host, so equivalent HTTP/1
and authority-only HTTP/2 requests both expose `example.test` and return 204.

`max_streams` now also bounds pending semantic/reset items. A new pending item
at the limit terminates with ENHANCE_YOUR_CALM and clears pending ownership.
Per-stream cancellation retains its bounded scan; HPACK encoding still happens
only in wire order and active borrowed output remains immutable. Deterministic
regressions exercise limits 4, 16 and 64 with output withheld and verify resource
cleanup. The optimized bounded probe feeds 5,000/10,000/20,000 malformed heads;
each stops at the default 128-item limit with only the 4,096-byte connection
control reserve remaining. Its original-engine run took 17.561/46.585/152.277 ms,
while the repaired run took 0.992/0.851/0.756 ms. Timing is descriptive; the
configured item limit, terminal code and cleanup assertions are the gate.

Response reservations now include retained semantic strings/vector storage,
HPACK encoded-string growth and primitive intermediates, and framed output.
The field vector is pre-sized, while active output reserves only its framed-wire
bound. The aggregate peak reservation lasts until output storage is freed;
refused/failed encodings free semantic and output storage before releasing it.
A 60,000-byte field is refused under a 150,000-byte child response budget before
unadmitted copying/encoding. The allocation probe uses a 500,000-byte child
budget and measures actual live allocations using macOS `malloc_size`:

- Original engine: reservation 124,326 bytes, retained semantic allocations
  69,792 bytes, encoding peak additional 262,144 bytes; probe exits 3.
- Repaired engine: equal parent/child reservation 425,475 bytes, retained
  semantic allocations 69,792 bytes, encoding peak additional 196,608 bytes.
  Combined peak 266,400 bytes fits admission; wire output is 60,050 bytes.
  The probe exits 0 and verifies only the connection control reserve remains
  after output advancement. Allocator rounding is included in these figures.

Reset regressions decode exact stream destinations and four-byte error codes
for semantic/priority errors, stream/body/CONNECT refusal, unsupported response
bodies and invalid response lengths. Valid siblings still respond. The reported
`reset(3, internal_error)` mutation produces 46 failed checks in the strengthened
header suite. Peer RST regressions cancel a parked handler before a late resume,
release stream/head reservations, remove a queued response and preserve a
sibling's completion. The wire oracle now rejects trailing fragments, unfinished
blocks, nested HEADERS and orphan CONTINUATION, with explicit negative cases.

Groundwork TDD RED receipts: `/private/tmp/task140-fixer-headers-red.log` (four
oracle failures), `task140-fixer-exchange-red.log` (authority, pending-bound and
peak-budget failures), and `task140-fixer-allocation-red-exact.log` (allocation
assertion exit 3). The repaired focused TLS-off suites pass 22 tests / 2,587
checks; TLS-on adds the real TLS boundary suite for 25 tests / 2,772 checks.
Focused ASan/UBSan passes the same 22 engine tests / 2,587 checks without a
diagnostic. Sanitizer artifacts are `/private/tmp/task140-fixer-sanitizers/`.
Changed-source cpplint, Lizard CCN 10, file-size checks and `git diff --check`
pass. Final full repository gates remain coordinator-owned; nonlocal platform
checks remain CI/v3 PR-owned.
