# TASK-122: Native HTTP/1.1 WebSocket upgrade evidence

Base: `v3` / `e1b403e3116d645905c6f7141eb2aaec43ea339d`.
Task worktree: `.worktrees/TASK-122`, branch `task/TASK-122`.
Implementation receipts are retained in that worktree's `receipts/` and
durably copied with SHA-256 hashes to the repository's local
`.git/groundwork/implementation/task122/20261003T205953Z-resume/` directory.
Caller validation and merge are separate.

## Interface decision and failure boundary

Compared A (one awaitable owned session/result), B (upgrade plus separate
handler coroutine), and C (decision plus `take_session`). A hides
negotiation, admission and transfer behind one entry point; B adds task
launch/exception/lifetime seams; C splits ownership across public calls.
A gives the best depth and keeps negotiation changes local. It follows
architecture §3.1 without making TCP delivery a public completion promise.

The frozen decision is one bounded queue-admission transaction that
transfers stream ownership to the shared driver. Structural/policy
refusal leaves the exchange and slot nonterminal. All potentially
throwing driver/tail/output allocations precede wire commitment.
A successful commit returns a move-only semantic session and owned
selected protocol. Cancellation/transport failure after commitment
closes the connection; a second HTTP response is never committed.
The full ordered HTTP outbox must be empty before frame output starts.
A trusted fixed 101 path preserves `Connection: Upgrade`; the ordinary
framer keeps its hop-by-hop sanitization.

```
HTTP head -> lazy route decision -> refusal -> ordinary HTTP response
                               -> accepted 101 -> pending head flush
                                                -> WebSocket -> terminal
```

The engine owns the driver and the sole socket reader/writer. The route
frame owns the live exchange/sinks through handler completion. Parser
residue and staged input transfer once, in order. The reader retains the
exact unconsumed suffix and retries it before newer socket bytes.
Bodyless routed heads freeze parsing and bound staging to the staging
cap plus one fixed read buffer. Transfer cancels a leased already-pending
read so a buffered first frame cannot wait for newer socket traffic.

Protocol mutations never hold the engine mutex. Progress notifications
copy a shared callback under the session mutex and invoke it outside that
mutex. A weak engine reference avoids a cycle. Snapshot reads may take
the session mutex under the engine mutex because the inverse mutation
edge does not exist. Condition checks and wake registration share the
engine mutex; input/output state mutates before the ordered wake.
Socket read/write and watchdog registration are also ordered against
shutdown, preventing operations from being registered after release.

## Handshake and resource policy

The pure helper consumes the authoritative semantic head, checks all
field occurrences, and reuses in-tree SHA-1/canonical Base64. GET,
HTTP/1.1, a valid Host, exact Upgrade/Connection tokens, one 16-byte key,
version 13, and empty/unambiguous body framing are mandatory. Transfer-
Encoding and Expect conflict. Extensions are bounded ignored metadata.
Origin/subprotocol policies and refusal examples are documented in
`http1-migration.md`; no unvalidated request value is reflected. Head
bytes/fields and 256 list/policy entries bound work and allocation;
route session limits cannot enlarge connection resource caps.

Handshake flush uses a fixed handshake deadline even when the owning
session is abandoned. Queued data uses write-idle. HTTP header/body
inventory no longer governs upgraded traffic. Application Close must
retain the session through terminal notification. Abrupt stop/failure
is safe; TASK-123 owns graceful server WebSocket drain.

## TDD and doubt closure

| Named doubt | Observed evidence |
|---|---|
| Refusal or dormant lazy upgrade consumes terminal state | `websocket_upgrade_decision`: owned options, delayed-start recheck, nonterminal refusal, suspension preservation, double decision |
| Hostile fields allocate before bounds | `http1_websocket_handshake`: malformed/cardinality/key/origin/protocol/body tables, byte bounds; explicit 257-token/policy RED then GREEN |
| Buffered frames are lost, duplicated, or parsed as HTTP | Initial external coalesced-input lane timed out; leased-read repair passes every handshake split, three coalesced frames, one-message incoming cap suffix retry, and HTTP-looking frame rejection |
| Frames overtake 101 or previous responses | Trusted outbox commit/order/capacity tests; shared codec partial-output tests; external wire parses full 101 before every echoed frame |
| Sleeping loops miss send/input-capacity changes | Reentrant private observer and receive-capacity tests, idle unsolicited send, incoming-capacity suffix retry, actual paused-reader backpressure/resume |
| Callback/driver retains engine or strands waiters | Native shutdown test observes pending receive failure, one close notification and expired weak engine witness; stalled writable abort observed externally |
| Route ends during delayed 101 | Native small-send-buffer/32 KiB protocol test first failed to stop; fixed handshake deadline stops it around 32 ms; external handler return/throw flush 101 then close |
| Shutdown registers I/O after transport release | Native lifetime RED failed stop/weak expiry; read/write/timer registration under engine mutex makes it GREEN |
| Published resume waiter dies before timeout registration finishes | Native clean-Close ASan caught a READ UAF; deterministic actual-API allocation interleaving caught a WRITE UAF twice; retained local/shared state and mutex-ordered ticket installation pass signal and cancel (1 test / 8 checks) |
| Crypto introduces native dependencies | Published RFC accept vectors; external accept check; dedicated native archive/audit lane below |
| HTTP framing or close regresses | Existing focused/full HTTP suites below; interim queue-accounting regression and repair described below; no TASK-123 claim |

The new writer's condition recheck exposed an inherited outbox bug:
starting the final head counted already-emitted 100 bytes again.
`interim_consumption_releases_all_queue_capacity` observed an empty
outbox with `queued_bytes == 25` (RED); final-head delta accounting makes
it zero (GREEN). The writer also checks actual front-byte availability,
so later ordered slots cannot make an empty front spin. Admission and
budget invariants are unchanged.

The clean peer-Close sanitizer lane also exposed an inherited
`resume_waiter::await_suspend` use-after-free after publishing its node.
A deterministic allocation hook drives the actual signal/timer APIs and
reclaims the published waiter before timeout registration returns;
ASan failed twice before repair. The awaiter now retains local state,
node, deadline and a shared ticket slot before publication. Installation
and destructor cancellation share the resume-state mutex; a delivered
or destroyed waiter cancels a late ticket. Post-publication work never
accesses the frame. The expanded repair is confined to
`concurrency/resume_signal.hpp` and its dedicated lifetime test; related
resume/body/route and sanitizer regressions are replayed below.
Debug journal: `.debug/resume-publication-task122/journal.md`.

The resumed observer sanitizer failure was a test lifetime error:
`progress`, `closes` and `wakes` were declared after the owning session,
so their scopes ended before session destruction notified the observer.
The failure was reproduced in `resumed-observer-asan-red.log`; declaring
those callback counters before driver/session preserves their lifetime
through teardown. `resumed-observer-asan-green.log` passes both tests
(17 checks) under ASan/UBSan with teardown notifications intact.

## Receipts

- Worktree baseline: codec/session/exchange decision and connection-engine
  binaries passed; engine 36 tests / 207 checks.
- Handshake RED: helper missing; GREEN 7 tests / 62 checks, including the
  RFC accept known answer and two explicit token-bound failures repaired.
- Owned decision RED: result/task/trusted commit APIs missing; GREEN 4
  tests / 31 checks, including interim accounting and strict accept-padding failures.
- Observer RED: progress/snapshot seam missing; GREEN 2 tests / 17 checks.
- Native lifetime RED: stalled head, shutdown and clean peer Close retained
  work; GREEN 4 tests / 20 checks, fixed head deadline, complete prior
  HTTP/101/frame wire ordering, and expired weak-engine witnesses.
- External client: isolated Python environment, `websockets==15.0.1`.
  `build-task122/client-env/bin/python test/integ/native_websocket_client.py
  build-task122/test/native_websocket_fixture --independent` passes
  handshake/origin/subprotocol, text/binary, Ping/Pong, clean Close and
  extension omission; external paused reads produce actual backpressured
  sends and resume 64 ordered MiB. The stdlib-only wire lane independently
  covers malformed 400/403/426, all splits, coalesced frames, queue-full
  suffixes, HTTP-looking bytes, idle send wake, return/throw and blocked
  writable abort. Default Automake E2E uses that stdlib lane; it requires
  no external client package.
- Fresh final gates (Apple clang 21.0.0, C++20, Darwin arm64):
  `resumed-full-check.log` records `make check -j1`, 216/216 PASS,
  zero skipped/failed/error, followed by passing check-local header,
  examples, README/release-note/hook docs, zero-warning Doxygen,
  staged install layout and backend header hygiene gates.
  `resumed-focused-check.log` records 27/27 PASS with no skips.
- `resumed-independent-client.log` and
  `resumed-asan-independent-client.log` pass every external/raw lane
  above, including real paused-reader saturation and ordered resume.
  ASan/UBSan focused consumers pass observer, session race, native
  transport, deterministic waiter lifetime, resume signal and task race.
  The separate `resumed-tsan-fresh-*.log` set rebuilds the entire native
  archive after the resume repair and passes the same six consumers.
  Earlier TSan archives predate that repair and are not final evidence.
- `resumed-native-linkage.log` passes the source and symbol audit.
  Transitional configured LDFLAGS list unused GnuTLS; the separate
  `resumed-native-no-thirdparty.log` recompiles/links/runs the actual
  upgrade probe without third-party flags, and `otool -L` lists only
  `libc++` and `libSystem`.
- `resumed-installed-v3-consumer.log` compiles/links/runs exchange and
  WebSocket consumers using staged installed headers and the native
  archive. `resumed-installed-examples.log` records 37 built, one
  legacy WebSocket example skipped because its legacy feature is
  disabled, and zero failures. Native upgrades remain enabled in this
  same build. `resumed-source-distribution.log` verifies the source
  tarball contains the handshake/bridge, native fixture, Python client,
  E2E shell entry point and waiter-lifetime regression.
- Changed-file cpplint, file-size, workflow gates and diff hygiene pass.
  Whole-source complexity and duplication still fail with exactly the
  baseline offenders (duplication paths normalized); no new offenders.
  `resumed-baseline-lint-comparison.log` retains that comparison.
  These baseline findings are disclosed for caller adjudication.
