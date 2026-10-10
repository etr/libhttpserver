# TASK-190 implementation evidence

Worktree `/home/etr/progs/libhttpserver/.worktrees/TASK-190`, branch `task/TASK-190`, base `v3` at `92f09185e28a8e19edeb7ed2435c21d8d318cc55`. Manual Groundwork implementation; caller owns validation, finalization, merge and cleanup. Task card/index remain In Progress. No configured executor memory store was found; reusable setup observations are retained in the invocation journal.

## Cause and repair boundary

Journal `.debug/h2-route-effect-ordering-s9j48boo/journal.md`; evidence root `build/task190`. The immutable four TASK-185 normal/ASan TLS-off/on logs and pinned H2 receipt were read and hashed in `retained-identities.json`. They contain five route-effect failures plus the aggregate 47!=52 assertion: six checks, one oracle cause. The fresh unchanged task-local baseline exactly reproduces those six checks (`baseline-red.log`, four tests, 944 checks, zero skips). Bootstrap/configure/library/baseline command logs are retained.

PRD-V3N-REQ-023 makes decisions possible at complete request headers; it does not require a lazy handler to execute before the parser's next frame. REQ-025 requires cancellation. Architecture §§3.3–3.4, DR-V3-001/003/006, `docs/task-140-http2-evidence.md` and `docs/task-142-http2-evidence.md` retain connection-owned HPACK/flow control and serialized, executor-affine guarded route resumes. The existing `http2_reset/peer_reset_invalidates_queued_and_parked_handlers_and_returns_reservations` explicitly requires zero calls if reset precedes executor start and one call if it already started.

`http2_request_state.hpp::admit` posts the route task. `reset/discard` disconnects it; subsequent owner reaping invalidates its queued frame witness. The original five `feed` entries parse both their valid opening and later error before running any pending handler, then incorrectly expect route 1. Running stale work or invoking handlers inline would violate the documented lifecycle. No production defect is demonstrated.

The minimal repair gives those same five entries the explicit `dispatch` stage. Executor turns occur only at completed protocol frames. The opening handler records its actual head and suspends on an application signal, leaving the request active until its later stream error cancels it. Output retirement remains deferred until the transcript is parsed. Every original route payload, RFC verdict and wire file is unchanged; the corpus still contains exactly 52 entries. Unknown stage values now fail explicitly.

A completed response is a distinct state: `prepare_response` marks the send side ended at the semantic response, and the next owner reap removes an END_STREAM request even before output retirement. The initial dispatch-only probe documents that `closed-data` and `half-closed-head` then correctly produce connection STREAM_CLOSED (`schedule-green-initial.log`). The final dispatch stage suspends those handlers rather than pretending a completed stream is still half-closed. Separate completed-response regressions retain and verify that connection verdict for both inputs across every fragmentation split. No wire-error oracle is weakened or made nondeterministic.

## Causal RED and discriminating GREEN

Before correcting the harness/metadata, new paired regressions passed all eight deferred controls but failed all eight started controls at `route effects`, yielding 14 failures together with the six original checks (`schedule-red.log`). This is the expected missing executor boundary, not a timeout or build error. Final GREEN logs retain the source identity and exact protocol observations.

A bounded task-local discriminator also compiles a copy of the final harness
with only dispatch-stage executor turns removed. It reintroduces all five
original route failures and all eight started-control failures, plus the two
aggregate completion-count failures (exit 15). `discriminators/receipt.json`
records the exact compile command and source SHA256
`9875ffd708a273c06f97c7de401e3b1553e594979aecdcf4ce18fd966a58e2fc`;
`discriminators/red.log` retains the assertions. The real harness and production
tree are untouched by this probe. Exact baseline source/metadata copies are
retained alongside it. This independently confirms the causal scheduling seam
after adding final diagnostic/count assertions.

For each named later error, paired schedules append an independently authored dynamic-table insertion on reset stream 1, then a sibling on stream 3 references index 62. They require its actual `x-route: after` field, `GET /sibling`, exact route order and reset-before-sibling-response ordering. All nonterminal suffix bytes are consumed. A skipped rejected block would instead lose dynamic state and fail the sibling decode/head assertion. Invalid-opening controls remain in the complete original corpus and still never route.

| Transcript | Required stream 1 error | Deferred route order | Started route order | Full suffix bytes |
|---|---|---|---|---:|
| content-length-data | PROTOCOL_ERROR (1) | 3 | 1, 3 | 144 |
| malformed-trailer | PROTOCOL_ERROR (1) | 3 | 1, 3 | 126 |
| closed-data | STREAM_CLOSED (5) | 3 | 1, 3 | 126 |
| stream-window-overflow | FLOW_CONTROL_ERROR (3) | 3 | 1, 3 | 129 |
| half-closed-head | STREAM_CLOSED (5) | 3 | 1, 3 | 153 |

Existing scalar diagnostics and test-local lifetime guards provide actual event order, without production instrumentation. Deferred: `admitted:1,cancelled:1,admitted:3,started:3`; admitted-but-cancelled is distinguished from never admitted. Started application wait: `admitted:1,started:1,cancelled:1,token-completed:1,destroyed:1,admitted:3,started:3`. Each started handler has exactly one destruction and one typed token completion (`cancelled`); its exchange disconnect reason is `connection_closed`. A late application signal executes no cancelled code. Three incomplete-body controls separately allow exactly one failed body-read completion (`connection_closed`), then one destruction and token cancellation. This is failure delivery before the next owner reap, not stale signal reentry; the body does not respond after failure.

Every variant requires exact RST_STREAM scope/code/count, no GOAWAY, complete suffix consumption and zero usage of all 13 resource kinds after engine/executor/route teardown. Connection-error controls require one GOAWAY, final output ordering, sticky failure and no duplicate output. Compression-error precedence remains in the unchanged `peer-reset-invalid-hpack`/`closed-invalid-hpack` entries. No production source, limits, client predicates, deadlines, exclusions or suppression inventory change.

The complete corpus executes **52 entries / 4352 variants / 4352 route checks** in each lane. The paired regressions execute **5 transcripts / 16 schedules / 2202 variants / 2202 route checks**; every schedule includes coalesced input, bytewise input, every split and three-byte output retirement. The two fully completed response controls add 193 replay variants. Within each fixed schedule, output bytes and consumed bytes must match exactly. Per-case and aggregate counts and actual event traces are printed in the test log; the littletest count does not include every throwing corpus predicate.

## Required local lanes

Fresh C++20 normal and ASan/nonrecovering UBSan lanes, TLS-off/on, use `/usr/bin/clang-18` and `/usr/bin/clang++-18` (18.1.3), `--disable-examples`, `-std=c++20 -pthread -O1 -g -fno-omit-frame-pointer`. TLS-on uses the existing `/home/etr/progs/libhttpserver/build/tooling/provider` include/lib and `-lssl -lcrypto`; it is not rebuilt. Sanitizers add `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fsanitize-address-use-after-scope`; all runs retain `ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, outside ptrace. No diagnostics are disabled. No production concurrency changes, so a new TSan lane is unnecessary; TASK-200 remains separate.

`build/task190/run_lanes.py` records configure/library/scoped-test/gate commands, environments, exits and durations in lane `receipt.json` files. The library builds first; TESTS/check_PROGRAMS/noinst_PROGRAMS are scoped together. All 18 existing H2 engine programs, HPACK corpus/connection, affected semantic owners, relevant public header consumers and v3_header_hygiene run in all four lanes (35 programs off, 36 on). TLS-on additionally builds the original `http2_tls_fixture` and runs `http2_tls_boundary`.

Normal and sanitizer TLS-on lanes run unchanged `scripts/run-v3-http2-gates.py` with `/tmp/task185-clients/bin/python`, Node `/tmp/task185-tools/node-v24.15.0-linux-x64/bin/node`, and `/tmp/task185-tools/h2spec`. Actual tool probes verify hyper-h2 4.4.1/hpack 4.2.0/hyperframe 6.1.0, Node 24.15.0 and h2spec 2.6.0 commit `70ac2294010887f48b18e2d64f5cccd48421fad1`. Each complete gate must retain both clients' multiplexing/reset/upload_flow/response_flow/websocket predicates and every nonexcluded h2spec inventory entry. The exact two RFC 9113 exclusions remain byte-identical (SHA256 `9256a4d025cb35a6056f142599ce34a29df8e70c4c857a9ab2cd472e11eeb4c3`). Gate-runner unit contracts, C++ lint, littletest result/skip contract and diff checks also run.

These gates exercise the existing **private negotiated-h2 engine TLS fixture**. They do not establish new public-listener H2 capability. No affected public runtime path changes, so no new installed public transport witness is claimed. TLS-off runs the complete engine/semantic selection without TLS negotiation. BSD/macOS/Windows and nonlocal backends remain unexecuted and CI/v3-PR owned under AGENTS.md.

`final-identities.json` hashes final test/corpus metadata, provider/tool identities and exclusions, and verifies all tracked production cpp/hpp and all 52 wire files against unchanged `v3`. The caller must archive `build/task190` and the unique `.debug` journal before cleanup. TASK-185's incomplete status and immutable artifacts, TASK-200 ownership, unrelated FEATURE worktrees and the clean primary checkout are preserved. No validation-loop, merge, cleanup, push, tag or publication is performed by this executor.

## Final local receipts

All four focused lanes pass with zero skips and no ASan/LeakSanitizer/UBSan diagnostics. Both unchanged pinned TLS-on gates pass **two independent clients, all five scenarios each, 145 of 147 h2spec inventory entries, and exactly two unchanged RFC 9113 exclusions**. Each independent client retains upload stall 65535 bytes, upload 163840 bytes, response stall 32 bytes, response 131072 bytes, cancellation count 1 and clean WebSocket close. Node reports nghttp2 1.68.0. Provider identity is OpenSSL 3.5.9 (29 Sep 2026).

| Lane | Programs | Littletest cases | Checks | Skips | Corpus entries / variants / route checks |
|---|---:|---:|---:|---:|---|
| normal-off | 35 | 333 | 31981 | 0 | 52 /4352 /4352 |
| normal-on | 36 | 338 | 32199 | 0 | 52 /4352 /4352 |
| asan-off | 35 | 333 | 31981 | 0 | 52 /4352 /4352 |
| asan-on | 36 | 338 | 32199 | 0 | 52 /4352 /4352 |

Each off lane contains 29 littletest programs plus six standalone header consumers; each on lane adds the five-case TLS boundary suite. The six-case engine harness reports 943 littletest checks in every lane, in addition to its explicit 4352 corpus and 2202 paired-schedule route predicates. Every lane executes the 16 paired schedules and 193 completed-response replay controls. Final source/object freshness is verified in all lanes; a nonfatal normal-on clock-skew warning is retained, and all four conformance objects are newer than the unchanged final source. Existing unrelated unused-helper compiler warnings are retained.

C++ cpplint passes the changed test. All six gate-runner contract tests pass; task-local Python tools compile; littletest skip/result contracts pass (77 all-skip, 0 mixed pass+skip, failure count for fail+skip); git diff --check passes. `final-summary.json` records per-program cases/checks/skips, per-lane source/object freshness, exact full gate receipts, final runtime log/executable/library hashes and unchanged frozen receipt verification. `final-identities.json` also verifies byte identity of the original H2 fixture, independent clients and gate runner/predicates against v3. The normal-off baseline configure/library logs precede the scripted matrix and remain separately identified.

No required local implementation check remains incomplete. Groundwork validation, final acceptance status, merge and cleanup remain caller-owned. TASK-190 action items are checked while card/index remain In Progress.

Final changed C++ source SHA256: `b2261aca6e26ef4fa1985d35d4e1f1ee34bd1e72c8362acdeda9e3fca762088e`.
