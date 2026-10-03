# TASK-121 codec/session design and evidence

The native public API is `httpserver::websocket`, independently includable and separate from the legacy upgrade API. HTTP negotiation and server drain remain TASK-122/123 work.

## Interface alternatives

1. A virtual protocol transport hides engine reads/writes behind callbacks. It couples coroutine and transport ownership, adds an interface to every protocol adapter, and spreads flow-control changes between implementations.
2. An owned shared core accepts ordered bytes with a consumed-prefix result and provides output-copy/output-consume. It hides RFC state, admission, queues, and coroutine completion; HTTP adapters only supply bytes and terminal transport events.
3. A caller-owned decoder plus queues minimizes internal ownership, but requires each adapter to implement admission, notification, and close state, repeating the most race-sensitive work.

Choose 2: greatest depth, all lifecycle and queue policy local to one core, and a seam at the ordered-stream boundary common to all three HTTP protocols. Handles are move-only; lazy operations copy shared state before the coroutine starts. The application surface exposes semantic send/receive/writable/close operations and one `on_close` registration slot. The non-installed `httpserver::detail::websocket_driver` owns ordered input, output-copy/output-consume, EOF, transport failure and cancellation. It transfers one semantic session using `take_session`; either owner teardown cancels shared state.

## Decisions and attempts to refute them

Use one mutex to serialize admission, waiter registration, and terminal transitions, and shared waiter nodes with frame witnesses for executor-posted resumption. Close callbacks and posted notifications run outside the mutex.

| Doubt | Disproving evidence required |
|---|---|
| Input length allocates before admission | Header-only oversize/overflow vectors; exact cap and aggregate-pressure checks |
| Empty messages bypass byte budgets | Incoming and outgoing message-count saturation and repeated drain/refill |
| Writable checks race registration | Concurrent drain/registration stress; capacity checked under same mutex as registration |
| Coroutine destruction leaves stale resume | Destroy parked and already-posted frames then drain executor; next wait still works |
| Session destruction invalidates lazy operations | Create lazy receive, destroy handle, then start operation |
| Terminal events deliver twice or callbacks deadlock | Feed/cancel races, duplicate EOF/cancel, simultaneous close, reentrant throwing callback |
| Controls split a partially emitted data frame | Fake driver copies/consumes one byte, feeds ping, compares complete remaining wire |
| Text validation loses fragment state | Split every codepoint across frames and byte-wise feed; invalid scalar corpus |

Wire vectors follow RFC 6455 sections 5.2-5.5, 7.4 and 8.1. Reserved diagnostic codes are never encoded on wire. The codec requires masked incoming client frames and emits no extensions.

These direct tests do not prove HTTP upgrades, independent clients, or server drain.

## Resumption and RED/GREEN evidence (2026-10-03)

Reused the registered `.worktrees/TASK-121` on `task/TASK-121`, base `v3` at `f25ecf2`, preserving the reboot-surviving implementation. The saved pre-reboot focused receipt was 11/11. The first fresh compilation exposed unfinished test-helper references to the private driver; completed that seam and the surviving single-close-callback registration tests. The resumed source was then refactored under green direct tests to meet changed-file formatting and complexity gates.

A new protocol regression, `session_answers_ping_while_local_close_awaits_peer`, reproduced suppression of Pong after local close initiation. RED: 12 tests, 405 checks, 404 successes and one wire comparison failure (`build/task121-ping-red.log`). Changed the reply guard from local `closing` to peer `peer_close`, as required by [RFC 6455 section 5.5.2](https://www.rfc-editor.org/rfc/rfc6455.html#section-5.5.2). GREEN: the same 405 checks passed (`build/task121-ping-green.log`). No unrelated connection-engine or server code was changed.

## Verification receipts

Commands run from the VPATH `build/` unless noted. Logs remain in the task worktree; this summary remains in the committed source.

- C++20 build: `make -j2`, configured with `CXX='clang++ -std=c++20'`, `CPPFLAGS=-I/opt/homebrew/include`, `LDFLAGS='-L/opt/homebrew/lib -lgnutls'`, passed. Transitional legacy links emit existing macOS `-bind_at_load`/duplicate `-lc++` warnings.
- Focused: `make -C test check-TESTS -j1 TESTS='websocket_codec websocket_utf8 websocket_session websocket_session_race consumer_v3_websocket task_race body_reader response_writer v3_header_hygiene v3_native_linkage consumer_v3_concurrency'`, **11/11 passed**, no Automake skips (`task121-focused-final.log`). Programs were rebuilt before this invocation.
- Direct sanitizer executables were built from each `test/unit/{websocket_codec,websocket_utf8,websocket_session,websocket_session_race}_test.cpp` plus the four new implementation TUs, using `clang++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -Isrc -Itest` from the worktree root. All passed: codec **263093**, UTF-8 **282**, session **405**, race **2026** checks. No AddressSanitizer or UBSan diagnostics (`build/task121-sanitizers/*.log`). These direct suites start no HTTP servers.
- Supported race tooling: the same direct race-suite build with `-fsanitize=thread` passed **7 tests / 2037 checks**, with no ThreadSanitizer reports (`build/task121-sanitizers/tsan.log`). Race-dependent check totals can vary because registration and terminal events may win in either order.
- `make check-v3-native-linkage`: both source and binary-symbol audits passed. The configured GnuTLS dylib is listed by the environment link flags but resolves no symbols in the audited native executable; only platform/C++ runtime symbols are required (`task121-linkage.log`). The probe constructs both the driver and semantic session and consumes encoded output.
- `make check-local -j1`: passed header compilation, examples, repository invariant lint, native linkage, README/release-note checks, zero-warning Doxygen, hook docs, shared staged install layout and consumer header hygiene (`task121-check-local.log`). Private driver/codec/state headers are distributed but not installed; semantic WebSocket headers are installed.
- `python3 -m cpplint --extensions=cpp,hpp --headers=hpp` over every changed `.cpp`/`.hpp`: passed, no diagnostics (`build/task121-cpplint.log`). `python3 -m lizard -C 10 -w` over the new WebSocket implementation and headers: passed, no function above the repository CCN ceiling (`build/task121-local-complexity.log`). `make lint-file-size`: passed.
- Full suite: the initial sandboxed attempt could not bind/connect localhost (test diagnostics explicitly reported the sandbox local/private-network policy), so it was stopped and rerun with approved local-network access. The first complete authorized `make check -j1` finished **209/210**, with only the unchanged `connection_engine::eof_before_request_closes` timing out after ten seconds at `test/unit/connection_engine_test.cpp:494` (`task121-full-first-complete.log`, `task121-connection-engine-first.log`). The isolated unchanged `test/connection_engine` then passed **36 tests / 207 checks** (`task121-connection-engine-rerun.log`). Final serial `make check -j1` rerun returned **0**, with **210/210 passed**, no Automake skips/failures/errors, and all recursive check-local/header/install/hygiene/docs gates passing (`task121-full-rerun.log`). The unchanged connection-engine suite passed all 36 tests / 207 checks in that rerun.

`git diff --check` passed after completion/status documentation updates. Implementation action items are complete; coordinator validation and merge are separate next steps.

## Existing repository-wide lint failures

`make lint-complexity` fails on unchanged `dispatch_request` (CCN 11), native server `listen` (11), `valid_peer_pattern` (15), and `accept_one` (11). New WebSocket functions pass the same ceiling. Exact output is in `build/task121-complexity.log`.

`make lint-duplication` fails on unchanged pairs: `body_reader.hpp:157` / `response_writer.hpp:162` (144 tokens) and `concurrency/task.hpp:532` / `concurrency/task.hpp:615` (133 tokens). No TASK-121 files appear in the report (`build/task121-lint.log`).

`git diff --exit-code f25ecf2 -- src/detail/request_lifecycle.cpp src/detail/server.cpp src/httpserver/server/options.hpp src/httpserver/detail/io_poll_sys.hpp src/httpserver/body_reader.hpp src/httpserver/response_writer.hpp src/httpserver/concurrency/task.hpp` returned 0, verifying the reported source is identical to the task base. The failing connection-engine implementation and test are likewise unchanged. These repository-wide failures are recorded for coordinator adjudication; they are not described as passing gates or silently repaired.

## Evidence boundary

Coverage includes masked input, unmasked output, incremental canonical framing, unsupported RSV/opcodes, fragmented text and binary, interleaved controls, invalid scalar UTF-8, close payloads, declared/cumulative limits, byte/count queue admission and release, zero-length floods, partial writes, bounded control reservation, parked/lazy/abandoned waits, event/registration races, simultaneous close, terminal reasons, reentrant/throwing callbacks, and exactly-once notification. The legacy WebSocket transcript profile is unavailable in this build; native codec/session tests still execute. HTTP upgrade negotiation, independent-client interoperability and server drain remain TASK-122/123 and subsequent gate work.
