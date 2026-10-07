# TASK-141 implementation evidence

Implementation is isolated in `task/TASK-141`, based on `v3` at
`4ad941e0fd51c18ea550e30ec03aa622afe09cff`. The runner owns staging,
commits, validation coordination, merge and cleanup. Task and index remain
In Progress for that handoff.

## Behavior and resource bounds

The private HTTP/2 request engine now dispatches a complete request head before
END_STREAM, supplies the existing body reader/writer seams, and independently
tracks connection and stream receive/send windows. Bodyless `respond()` ends
HEADERS; `start_response()` leaves HEADERS open, with terminal DATA or trailers
coming from `finish()`. Existing sinks receive the streaming callback through
default delegation; the response_writer and exchange_decisions suites retain
one-head and terminal-decision coverage.

DATA is debited by its full payload, including padding. Body octets produce no
receive credit until `pull()` copies them; processed padding and discarded
reset-stream bytes are reclaimed at connection scope. Consumption is coalesced
in counters. Queuing a WINDOW_UPDATE reserves a control slot but does not
increase receive allowance. Its first output exposure publishes the allowance,
matching the connection's existing borrowed-output commit boundary. Published
slots remain immutable across partial advancement.

Initial SETTINGS advertises a zero stream receive window. The effective receive
window remains 65,535 until that SETTINGS is acknowledged, preserving legal
pre-ACK DATA. Each non-ended stream reserves a 65,535-byte receive ring plus
128 bytes before allocation; the connection allowance still bounds aggregate
unconsumed early DATA. Admission applies the smaller application cap (default
16,384, bounded to the configured engine cap). If already staged early data
exceeds that cap, the stream fails explicitly. After ACK, grants target admitted
free capacity and account for negative windows, unread bytes and queued grants.
A request larger than a two-byte policy buffer streams incrementally in tests.

The engine reserves a reusable 16,384-byte parser payload buffer plus 128 bytes
before admission, so a granted DATA frame does not require a new shared-budget
reservation. SETTINGS, PING and WINDOW_UPDATE use fixed control scratch storage.
Stream metadata is charged before construction (`sizeof(stream) + 64`), with
stream-count, head-byte and head-field reservations. Header assembly, HPACK,
trailers and response representations retain their separate hierarchical
charges. Child refusal rolls reservations back; destruction tests assert zero
remaining stream/body/head/output usage.

Response staging defaults to a fixed 16,384-byte ring plus 128 bytes. Both body
queue limits accept 1..65,535. An active DATA frame reserves its copied payload,
framed output and allocation allowance before allocating. Queue capacity is
freed when framed bytes retire; window credit is debited once when selecting a
frame. Partial advancement neither frees an unfinished frame nor double-debits
windows. One bounded DATA frame per eligible stream selection provides round
robin progress; controls take priority between frames. Open field blocks and
exposed DATA remain immutable through reset or connection failure.

Request/response content-length uses checked unsigned decimal parsing with
consistent duplicates and actual body totals. Request trailers require terminal
HEADERS, reject pseudo/forbidden fields, retain repeated fields, and decode with
the same connection HPACK state. Response trailers encode after queued DATA in
transmission order. HEAD/304 metadata and body-forbidden statuses retain their
constraints. Empty terminal DATA is allowed without available window credit,
including a negative stream window after SETTINGS, per
[RFC 9113 section 6.9.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-6.9.1).

Completed handlers retain pending streaming output. An unfinished streaming
response resets on handler completion. Reset/failure disconnects the exchange,
wakes/detaches parked reads/writes, invalidates coroutine witnesses, removes
unexposed semantic output, refunds discarded connection credit, and preserves
already exposed wire storage. Handler-triggered failures defer destruction to
an owner pump.

## TDD observations

Behavioral RED runs (all compiled successfully) were captured under
`/private/tmp/task141-*.log`:

- `task141-red.log`: open POST never dispatched; streaming writes failed and
  emitted a reset instead of HEADERS/DATA (10 failed checks).
- `task141-red2.log`: additionally, independent stream-window relief and a
  paused reader with sibling/PING progress failed (13 failed checks).
- `task141-trailer-red.log`: response trailer finish reset the stream instead
  of producing terminal HEADERS (2 failed checks).
- `task141-storage-red.log`: admitted DATA failed after shared body-budget
  capacity was consumed elsewhere (3 failed checks). Reusable parser storage
  made the same test pass, while WINDOW_UPDATE remained processable.
- `task141-length-red.log`: a forbidden 204 content-length incorrectly finished
  successfully (2 failed checks), then failed with the intended stream reset.
- `task141-limits-red.log`: zero body queue capacity was accepted at construction
  (1 failed assertion), then rejected with invalid_argument.
- `task141-setting-red.log`: an intermediate repeated SETTINGS value overflowed
  the live send window but feed failed to report the terminal error immediately
  (1 failed check), then returned the connection flow-control error.
- `task141-empty-red.log`: empty END_STREAM after acknowledged receive-window
  reduction reset a valid stream (3 failed checks), then drained the body and
  produced its response.
- `task141-output-red.log`: failure to admit active DATA storage left GOAWAY
  waiting for another event (1 failed check), then emitted it in the same pump.
- `task141-visibility-red.log`: trailers were visible before the body reader
  observed EOF (1 failed check), then stayed hidden until EOF.
- `task141-early-policy-red.log`: completed early data bypassed a smaller
  admission cap (2 failed checks), and DATA after receive EOF used PROTOCOL_ERROR
  instead of STREAM_CLOSED (1 failed check). Both now reset only their stream
  with the intended code while the sibling completes.

Predecessor tests expecting unsupported request/DATA response behavior were
updated to assert current streaming behavior and unfinished-response failure.
The wire oracle keeps strict END_STREAM checking by default; streaming sections
require an explicit opt-in and retain truncation/continuation validation.

## Local verification commands

The actual build environment is local macOS/arm64, C++20. Bootstrap's existing
Automake/Autoconf warnings, unavailable microhttpd_ws.h, deprecated linker flag
warnings and configure's restricted sysctl probe did not prevent builds.

```sh
./bootstrap
mkdir -p build-off build-on build-sanitize
(cd build-off && ../configure --disable-v3-tls \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g' && make -j2)
(cd build-on && ../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g' && make -j2)
```

The reused TLS provider was verified as OpenSSL 3.5.9 (29 Sep 2026).
For each build, the focused programs were built with `make -C <build>/test -j2`
and run with `make -C <build>/test -j1 check-TESTS`, assigning both
`check_PROGRAMS` and `TESTS` to:

```text
http2_flow_control http2_streaming http2_headers http2_exchange http2_frame
http2_settings http2_connection http2_conformance hpack_connection
hpack_field_section body_reader response_writer exchange_decisions
exchange_routing http1_body_source http1_response_outbox
```

The TLS-on list also includes `http2_tls_boundary`. Its real TLS-adapter case
segments incoming POST DATA, stalls outgoing DATA at zero peer stream credit,
processes WINDOW_UPDATE over TLS, and verifies exact decrypted response bytes.
This proves the private TLS/HTTP2 composition, not public-listener h2 dispatch.

Both builds also run:

```sh
make -C <build> check-v3-native-linkage check-local
```

Sanitizer configuration and focused gate:

```sh
(cd build-sanitize && ../configure --disable-v3-tls \
  CPPFLAGS=-I/opt/homebrew/include \
  LDFLAGS='-L/opt/homebrew/lib -fsanitize=address,undefined' \
  CXXFLAGS='-std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer')
make -C build-sanitize/src -j2
make -C build-sanitize/test -j2 http2_streaming http2_flow_control http2_exchange
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  make -C build-sanitize/test -j1 check-TESTS \
  check_PROGRAMS='http2_streaming http2_flow_control http2_exchange' \
  TESTS='http2_streaming http2_flow_control http2_exchange'
```

The initial `detect_leaks=1` attempt aborted before tests because LeakSanitizer
is unsupported on this macOS runtime. ASan and UBSan remain enabled; LSan is
unexecuted. The sanitizer build has a local ignored-artifact marker.

Changed-source/test cpplint uses the existing offline installation:
`python3 /private/tmp/task129-lint/cpplint.py --extensions=cpp,hpp <changed C++ files>`.
Changed HTTP/2 code uses `python3 -m lizard -C 10 --warnings_only`;
`bash scripts/check-file-size.sh` and `git diff --check` also run.

The repository-wide `scripts/check-complexity.sh` still reports three unchanged
baseline functions: `dispatch_request` (CCN 11), `valid_peer_pattern` (CCN 15),
and `pollsys::accept_one` (CCN 11). The same findings were reproduced from
`git show 4ad941e:<path>` copies in `/private/tmp`; TASK-141 does not alter them.
Changed HTTP/2 code stays within CCN 10 and the existing 500-SLOC gate.

BSD, Windows and other nonlocal checks were not executed. They remain CI/v3 PR
owned under AGENTS.md. No public deployment, listener cutover or independent
external-client interoperability claim is made by this implementation gate.

## Final local results

All final commands completed with exit code zero except the documented
repository-wide complexity baseline probe:

| Gate | Result |
| --- | --- |
| C++20 TLS-off build | Passed |
| TLS-off focused suites | 16/16 passed, no skips |
| C++20 TLS-on build | Passed |
| TLS-on focused suites | 17/17 passed, no skips |
| ASan/UBSan focused suites | 3/3 passed, no sanitizer findings |
| TLS-off native-linkage and check-local | Passed |
| TLS-on native-linkage and check-local | Passed |
| Changed C++ cpplint | Passed |
| Changed HTTP/2 Lizard CCN 10 | Passed |
| Repository file-size gate | Passed |
| git diff --check | Passed |

The final streaming suite executes 18 cases / 273 checks; flow control executes
6 cases / 85 checks; the modified exchange suite executes 16 cases / 436 checks;
the TLS boundary executes 4 cases / 202 checks. Final build/test/local-gate logs
are `/private/tmp/task141-complete-{off,on,san}-*.log`, with static receipts at
`task141-final-cpplint.log`, `task141-changed-complexity.log`, and
`task141-file-size.log` in the same directory. Independent validation and
runner-owned publication remain the caller's next phase.
