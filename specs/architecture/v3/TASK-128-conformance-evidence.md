# TASK-128 HTTP/1 and WebSocket conformance evidence

Implementation branch: `task/TASK-128`, base `v3`, initial revision
`4c9a52ed0fc99afce59cfca767fabf1a6c88055f`. This document records local
implementation checks; the caller owns validation and finalization.

## Coverage and requirements

| Requirement | Gate and observed behavior |
|---|---|
| REQ-004 | 46 named RFC 9112 binary fixtures; real head parser, framing selector, body decoder and native loopback engine. Whole, bytewise and every-split replay checks payloads, typed errors, close posture, sticky failure, bounded staging and consumption. Native rejected requests cannot dispatch the appended sentinel; valid controls dispatch both requests. Truncated bodies are admitted before native EOF. Exact/over head budgets and pipelined parser residue are checked separately. |
| REQ-010 | Existing handshake/upgrade/decision/progress suites, independent and raw clients; native 101 completes before the input plateau fixture starts its workload. |
| REQ-012 | 32 RFC 6455 codec fixtures, bounded queue admission/dequeue/resume, native HTTP upload/output and WebSocket input/output plateaus; independent client socket saturation and 64 ordered MiB/retry. |
| REQ-013 | Native rejection close, malformed body terminal unwind, resumed peers, clean WebSocket close, abort, exactly-once stop/close callbacks, zero drain scope and weak engine expiry; existing session/drain/race suites and clients. |
| REQ-038 / DR-V3-001 | Native HTTP/1 parity plus routing, hooks, Basic/Digest auth, URL-encoded/multipart forms and IP-control committed transcript replays, including both sanitizer lanes. Existing approved migration differences and v2 pins are preserved. |

The HTTP case table distinguishes head syntax, framing, body decoding, EOF,
Host semantics and valid controls. Host syntax accepts reg-name, IPv6,
IPvFuture, numeric ports and an empty field value; HTTP/1.1 requires exactly
one Host. Equal duplicate and comma-list Content-Length remain rejected by
v3's previously approved strict framing policy. Chunk extensions retain BWS
and quoted-string support. No transcript expectations were changed.

The WebSocket applicability is RFC 6455 over HTTP/1.1 with client masking and
no negotiated extensions. It covers RSV/reserved opcodes, minimal extended
lengths, masking, control fragmentation/length, continuation sequencing,
interleaved controls, split UTF-8, invalid text/close reasons, Close payload
length/codes, message caps and queue admission. These are applicable codec
checks, not a claimed full Autobahn run. Compression and HTTP/2/3 are later
milestones; TLS parity is deferred.

## RED and repairs

- Runner contract tests initially failed for missing runners. A later
  whitespace-only selection returned success; its new test failed before
  the runner was changed. A source-archive probe returned Git's status 128
  instead of its selected executable's status 7; optional revision metadata
  now explicitly labels unavailable Git metadata. Missing executable/client/tool, empty selection,
  and executable exit-status/log preservation now pass (seven Python checks
  across the two runner suites).
- Missing corpus runs failed by name before binary fixtures were added.
- `http:///` was accepted by the head parser. Its all-split corpus test
  failed; empty absolute-form authority now rejects.
- Missing/duplicate Host reached the handler and the appended sentinel.
  Native tests failed before Host validation was added. An invalid bracketed
  IP literal also reached dispatch; native address parsing now validates it.
- Malformed chunk bodies left an unfinished response slot holding the
  writer open. The native corpus timed out on `bad-chunk`; all disconnected
  exchanges now abandon their unfinished slots while preserving queued
  bytes for flush.
- A stalled admitted upload retained 110,592 then 780,288 pending bytes
  across the two attempted-work windows (observed tool output). Admission
  had no body backpressure gate. The reader now stops at staging/tail bounds,
  replays retained bytes before further reads, and resumes on released room.
  A second failing resume probe exposed notify-before-park: room callbacks
  now acquire the engine registration mutex after releasing the body mutex,
  and the locked reader posture retries any eligible tail.
- ASan caught `stack-use-after-scope` in the existing outbox corpus's
  coroutine helper: a temporary string was passed by reference across
  suspension. The helper now owns its payload by value.
- Fixture corrections are recorded separately from library repairs: the
  input plateau fixture drains the 101 response so its tiny socket buffer
  can admit the later Close reply, and retains the session through its Close
  callback. TSan exposed a test abort racing an active HTTP decision after
  an early delivery publication; the fixture now waits for the HTTP exchange
  to settle before its abort, respecting the exchange decision threading
  contract. The production exchange concurrency surface was not broadened.

Raw RED logs and sanitizer traces are retained under
`build/task128-evidence/`; build directories and logs are task-local ignored
artifacts, not committed generated output.

## Plateau mechanism and bounds

All four scenarios use real socket transport and configured small queues.
Upload stalls the application reader; HTTP output drives the engine's actual
outbox sink through the existing friend seam; WebSocket input stalls receive;
WebSocket output drives the session attached to the engine while its peer
stalls. Producers reuse fixed buffers. Full staging, kernel `would_block`,
and stable output admission (20 ms with deadline-bounded probes) establish
backpressure before sampling. Attempt windows are 1 MiB then 8 MiB.

- HTTP body and outbox budgets: 1,024 bytes each; outbox measurement permits
  fixed serialized-head overhead up to another 1,024 bytes.
- WebSocket receive budget: 512 bytes / eight 64-byte messages; send budget:
  522 bytes, with seven 66-byte frames reaching backpressure.
- Pending input tail: staging cap plus at most one 16,384-byte read buffer;
  the writer's scratch buffer is a fixed 4,096 bytes.
- Actual retained process RSS is sampled with macOS `task_info` or Linux
  `/proc/self/statm`. The second window may add at most 1 MiB of fixed
  allocator/sanitizer slack. This is smaller than the additional 8 MiB
  attempted window and catches hidden proportional retention. No allocation
  operators are overridden; sanitizer ownership is preserved. Kernel socket
  buffers are separate from RSS and bounded by the fixture's socket settings.

The fixture verifies admitted byte/frame delivery after resumption, queue
release, terminal callbacks exactly once, drain scope zero, and weak engine
expiry. This measures retained application/process memory, not a throughput
benchmark or an exact per-allocation census.

## Repeatable commands and toolchain

Normal configured builds use C++20 with Apple clang 21.0.0. Instrumented native
builds and fuzzing use Homebrew clang 22.1.8. The isolated independent client
is `websockets==15.0.1` in `build/client-venv` (Python 3.9).

```sh
./bootstrap
mkdir -p build
(cd build && ../configure --disable-examples --disable-doc \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib)
NO_PROXY=localhost,127.0.0.1 no_proxy=localhost,127.0.0.1 make -C build check -j1
make -C build/test check-v3-http1-websocket
scripts/run-v3-http1-websocket-gates.sh --build-dir build \
  --python "$PWD/build/client-venv/bin/python" --independent
ASAN_OPTIONS=halt_on_error=1:symbolize=0 UBSAN_OPTIONS=halt_on_error=1 \
  python3 scripts/run-v3-protocol-sanitizers.py --build-dir build/task128-asan-ubsan \
  --compiler /opt/homebrew/opt/llvm/bin/clang++ --sanitizer address,undefined \
  --python "$PWD/build/client-venv/bin/python" --independent
TSAN_OPTIONS=halt_on_error=1:symbolize=0 \
  python3 scripts/run-v3-protocol-sanitizers.py --build-dir build/task128-tsan \
  --compiler /opt/homebrew/opt/llvm/bin/clang++ --sanitizer thread \
  --python "$PWD/build/client-venv/bin/python" --independent
scripts/run-v3-protocol-fuzz.sh --build-dir build/fuzz-final \
  --compiler /opt/homebrew/opt/llvm/bin/clang++ --runs 2000 --seconds 30
```

Native sanitizer builds compile all 31 native translation units and all 29
selected consumers plus the fixture with matching instrumentation. Each test
runs from its build `test` directory so parity data resolve. No ordinary
archive is used as sanitizer coverage. Logs retain compiler, flags, revision,
dirty status and exact compile/link commands. The focused sanitizer CI steps
extend the existing broad workflow; one explicit Linux lane provisions the
pinned client and runs combined ASan/UBSan and libFuzzer.

Fuzz targets bound input to 64 KiB, staging/trailers/queues and iterations.
They assert consumption, sticky errors, terminal release and eligible
segmentation equivalence. Wide head-budget comparisons explicitly exclude
the parser's prospective whole-feed budget difference; tight budgets are
exercised separately. Normal deterministic replay uses the committed corpus.
Each libFuzzer target runs serially with seed 128, 2,000 runs, 30 s maximum,
5 s per-input timeout, 512 MiB RSS limit, writable seed copies and preserved
crash artifacts/reproduction commands. Explicit unsupported requests fail.

## Local outcomes and limitations

All executed checks below passed on macOS arm64. The full suite has one
expected Linux-only epoll skip. Final corpus assertions and runner contract
additions were also rerun directly after their last changes.

| Check | Outcome and retained log |
|---|---|
| Full C++20 configured `make check -j1` | 232 tests: 231 pass, one platform skip, zero failures; top-level header, examples, install-layout and hygiene checks also pass (`build/full-check-verified.log`). |
| Focused native protocol gate | All 29 executable consumers and raw clients pass (`build/focused-gate-final.log`). |
| Independent client 15.0.1 | All raw and independent wire/client scenarios pass, including real saturation and 64 ordered MiB (`build/independent-client-final.log`). |
| Native ASan + UBSan | All 31 core translation units and 29 consumers plus the native client fixture were rebuilt with matching instrumentation; full focused gate and independent clients pass (`build/asan-gate-verified.log`, `build/task128-asan-ubsan/build.log`). Final typed HTTP corpus assertions pass (`build/http1-corpus-asan-final.log`). |
| Native TSan | Same native source/consumer coverage; full focused gate and independent clients pass (`build/tsan-gate-final.log`, `build/task128-tsan/build.log`). Final HTTP, WebSocket and replay checks pass (`build/http1-corpus-tsan-final.log`, `build/websocket-corpus-tsan-final.log`, `build/seed-replay-tsan-final.log`). |
| Bounded libFuzzer | All three targets complete 2,000 runs with ASan/UBSan and the final committed fixtures; no crash artifact (`build/fuzz-final-output.log`, `build/fuzz-final/*/run.log`). |
| Runner contracts | Six gate-runner tests and one fuzz-runner test pass. |
| Distribution | `make -C build dist` passes, including the scripts and all 78 binary wire fixtures (`build/dist.log`). |

Retained queue totals and actual process RSS before/after the attempted
windows are recorded below. Values are bytes; all RSS growth is within the
fixed 1,048,576-byte slack, and every individual queue stays within its
configured bound.

| Scenario | Ordinary queues / RSS | ASan+UBSan queues / RSS | TSan queues / RSS |
|---|---|---|---|
| HTTP upload | 2,048/2,048; 1,835,008/1,835,008 | 3,072/3,072; 12,025,856/12,025,856 | 2,048/2,048; 42,024,960/42,352,640 |
| HTTP output | 1,024/1,024; 2,310,144/2,310,144 | 1,024/1,024; 30,752,768/30,769,152 | 1,024/1,024; 53,362,688/53,493,760 |
| WebSocket input | 1,562/1,562; 2,408,448/2,408,448 | 1,562/1,562; 31,440,896/31,440,896 | 1,562/1,562; 56,049,664/56,213,504 |
| WebSocket output | 462/462; 2,523,136/2,523,136 | 462/462; 31,916,032/31,916,032 | 462/462; 56,508,416/56,508,416 |

Rows come from `build/memory-final.log` and each instrumented build's
`protocol-gate-logs/native_protocol_memory.log`.

Initial sandboxed listener/curl checks failed because local listeners and the
configured proxy were restricted. Automatic escalation approved local test
execution and isolated client/tool installation; the actual local wire tests
were rerun outside that restriction. No sandbox failure is counted as a pass.

Changed C++ paths pass cpplint 2.0.2; changed production paths have CCN <= 10.
The whole-source complexity
gate still reports three unchanged baseline findings: `dispatch_request` 11,
`valid_peer_pattern` 15, and `accept_one` 11. Exact base-revision copies reproduce
all three; they are outside this task's diff. File-size and workflow-pinning
checks pass. BSD, Windows and other nonlocal receipts are unexecuted and owned
by CI/the v3 PR per AGENTS.md. RSS sampling is implemented for macOS and Linux;
other platform sampling/adapter failures are assigned to that CI/PR work.
Release-wide throughput, TLS, full Autobahn and cross-protocol hierarchical
budget audits remain with their later tasks.
