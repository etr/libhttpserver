# TASK-142 implementation evidence

Implementation is isolated in `task/TASK-142` at the registered worktree
`/Users/etr/progs/libhttpserver/.worktrees/TASK-142`, based on `v3` at
`d653e3cb84d351d1616cbeacaeabe4e85adfd98c`. Task and index remain In Progress.
The caller owns independent validation, commits, merge and cleanup.

## Behavior and bounds

Response DATA selection keeps the existing round-robin cursor and independent
connection/stream flow windows. A turn now stages at most 16,384 payload bytes,
even when peer MAX_FRAME_SIZE and the response ring are larger. After eight
other exposed output items, eligible DATA receives a turn. The counter saturates
at eight and resets upon DATA selection; blocked DATA does not delay controls or
pending response heads. A selected trailer block follows the same stream cursor.
The initial server SETTINGS precedes response output. An already exposed control
frame, DATA frame, or complete HEADERS/CONTINUATION field block finishes before
another selection, including on cancellation or terminal failure. HPACK encoding
still happens in wire order without a second scheduler queue.

Response rings retain their charged configured capacities (1..65,535 bytes plus
128 allocation bytes). Pending semantic responses/reset items remain bounded by
`max_streams`, and the engine retains one charged active output item. The maximum
DATA staging reservation is `2 * 16,384 + 9 + 128 = 32,905` bytes. The fairness
storage case verifies that partial advancement retains that charge, retirement
releases exactly that reservation, and peer reset returns body/output usage to
the connection baseline. Stream count, head bytes and head fields return to zero;
after engine destruction, body/output reservations also return to zero.

RST_STREAM on an idle odd client stream or any unsupported even server stream
terminates with connection PROTOCOL_ERROR. Peer reset invalidates queued or
parked handlers, requests cancellation, releases owned stream reservations and
removes unencoded semantic responses without a reset echo. Local abort requests
cancellation once and defers coroutine destruction to the next owner pump, after
its current resume returns. Fixed-slot compaction removes unexposed stream
WINDOW_UPDATE grants while preserving the exposed head's storage and offset.
Already exposed DATA remains immutable and retains its committed send-window
credit. Closed-stream HEADERS still decode to maintain HPACK state, then discard;
late closed-stream DATA restores connection credit without repeated reset output.
Tests reclaim six late DATA bytes exactly once and complete a sibling response.

The connection now owns two constant-storage temporal counters:

- All completed non-DATA frames consume control-work allowance, including ACKs,
  RST_STREAM, PRIORITY, WINDOW_UPDATE, extensions, HEADERS and CONTINUATION.
  Recoverable malformed non-DATA frames also consume allowance. Existing fatal
  protocol errors retain their error scope. DATA is excluded.
- Each newly opened odd request stream consumes opening allowance before header
  assembly/admission/handler scheduling, including subsequently refused, reset or
  completed streams. Continuations, trailers and closed streams do not count as
  new openings. Reset does not refund tokens.

Defaults are 256 non-DATA events and 128 stream openings per one-second interval.
These are configurable internal policy choices, not HTTP/2 protocol requirements;
`http2_request_limits::connection` forwards the complete connection limits.
Fields are appended to preserve prior aggregate initializers. Fixed-duration
windows refill on the first event at or after the interval. Adjacent boundaries
can permit twice the allowance in a short burst. Backward time cannot refill or
move the anchor; unsigned tick subtraction handles the entire signed clock range.
Pump turns, queue drains and output advancement never refill counters. Owners
supply a steady-clock timestamp to `feed()` for temporal refill; the existing
zero default remains a deterministic timestamp rather than a clock read.

Exhaustion uses reserved terminal storage for one GOAWAY with
ENHANCE_YOUR_CALM (11) and typed `limit_exceeded`. Queued dispatches become no-ops,
pending response work retires, and GOAWAY reports the last admitted stream.
The existing frame-per-turn, queue, byte and resource limits remain in force.
Invalid zero allowances or nonpositive intervals fail construction with
`invalid_argument` before dispatch.

## TDD observations

All RED regressions compiled and ran against the relevant preceding behavior:

| RED log in `/private/tmp` | Behavioral failure |
| --- | --- |
| `task142-fair-red.log` | 28 checks failed: oversized DATA, tiny response completion behind the large response, and starvation under replenished headers/PING ACKs. |
| `task142-reset-red.log` | 4 checks failed: idle reset acceptance, retained unexposed grant for the reset stream, and reset amplification on closed traffic. |
| `task142-rate-red.log` | 4 checks failed: drained PING/SETTINGS floods and completed stream churn had no persistent finite limit. |
| `task142-closed-rate-red.log` | 2 checks failed: ignored closed HEADERS/CONTINUATION could evade control-work allowance. |

GREEN coverage includes exact boundaries/refill, backward and extreme timestamps,
byte-fragmented frames charging once, each control type and recoverable error,
reset/refusal/completion churn, trailers excluded from openings, uncharged DATA,
queued-handler cancellation, local abort lifetime, borrowed partial output,
HPACK continuity, sibling progress and budget return to baseline/zero.
The new suites execute 3 fairness cases / 268 checks, 5 reset cases / 108 checks,
and 9 rate cases / 353 checks. Existing streaming, flow-control, exchange and
header suites retain zero-window, empty terminal DATA, field-block atomicity and
cancellation coverage.

## Local commands and results

Actual environment: macOS arm64, Apple clang 21.0.0, C++20. The reused provider at
`/private/tmp/task129-provider/install` was read back as OpenSSL 3.5.9
(29 Sep 2026). Bootstrap/configure's existing warnings, unavailable
microhttpd_ws.h and deprecated linker flags did not prevent builds.

```sh
./bootstrap
mkdir -p build-task142-off build-task142-on build-task142-sanitize
(cd build-task142-off && ../configure --disable-v3-tls \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g' && make -j2)
(cd build-task142-on && ../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g' && make -j2)
(cd build-task142-sanitize && ../configure --disable-v3-tls \
  CPPFLAGS=-I/opt/homebrew/include \
  LDFLAGS='-L/opt/homebrew/lib -fsanitize=address,undefined' \
  CXXFLAGS='-std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer')
```

The configured build directories contain local ignored-artifact markers; the
repository `.gitignore` is unchanged. Native libraries were rebuilt before tests.
Focused programs were built using `make -C <build>/test -j2 <programs>` and run
with `make -C <build>/test -j1 check-TESTS`, setting both `check_PROGRAMS` and
`TESTS` to the following list:

```text
http2_fair_output http2_reset http2_rate_budget http2_streaming
http2_flow_control http2_exchange http2_headers http2_frame http2_settings
http2_connection http2_conformance hpack_connection hpack_field_section
body_reader response_writer exchange_decisions exchange_routing
```

TLS-on also runs `http2_tls_boundary`. ASan/UBSan runs fairness, reset, rate,
streaming, flow-control and exchange with `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`. LeakSanitizer is unsupported on this local macOS
runtime; it was not run. Both ordinary builds run
`make -C <build> -j2 check-v3-native-linkage check-local`.

| Gate | Result |
| --- | --- |
| Baseline C++20 build and existing streaming/flow/exchange suites | Passed |
| TLS-off focused suites | 17/17 passed, no skips |
| TLS-on focused suites including TLS boundary | 18/18 passed, no skips |
| ASan/UBSan focused suites | 6/6 passed, no findings |
| TLS-off/on native linkage and check-local | Passed |
| Changed C++ cpplint | Passed |
| Changed HTTP/2 Lizard CCN 10 | Passed |
| Repository file-size gate | Passed |
| `git diff --check` | Passed |

Static commands:

```sh
python3 /private/tmp/task129-lint/cpplint.py --extensions=cpp,hpp <changed C++ files>
python3 -m lizard -C 10 --warnings_only <changed HTTP/2 sources>
bash scripts/check-file-size.sh
git diff --check
```

The separate repository-wide `scripts/check-complexity.sh` probe still reports
three unchanged baseline functions: `dispatch_request` (11),
`valid_peer_pattern` (15), and `pollsys::accept_one` (11). Identical findings were
reproduced from `git show d653e3cb:<path>` copies. Changed HTTP/2 sources satisfy
the threshold. These inherited findings remain with their existing component
owners/v3 maintainers; TASK-142 does not change those files or claim that the
repository-wide complexity probe passed.

Final logs are `/private/tmp/task142-final2-{off,on,sanitize}-tests.log`;
ordinary local gates are `task142-final-{off,on}-local.log`. Static and baseline
receipts are `task142-{cpplint,complexity,file-size,full-complexity,baseline-complexity}.log`
in the same directory. Build/bootstrap/configure and behavioral RED logs remain
there as task-local evidence.

BSD, Windows and other nonlocal platform checks are CI/v3 PR owned under
AGENTS.md and were not executed. This proves private-engine and local TLS-adapter
composition, not public-listener HTTP/2 dispatch, deployment or external-client
interoperability. No later task was started.
