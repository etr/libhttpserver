# TASK-123: WebSocket cancellation and server drain evidence

Base: `v3`, `e1fb7cb4f812e476302bde7d624fc30f8c66ad46`.
Implementation worktree: `.worktrees/TASK-123`, branch `task/TASK-123`.
Accepted plan SHA-256: `6c93e0e9923e7486c9ed1bf6ce651535c2b79bd598a1a3de3a9f51b411cf7b44`.
Task-owned receipts are in `_task123-receipts/`; fresh instrumented outputs
are in `_task123-asan/` and `_task123-tsan/`. These generated artifacts
are excluded from the implementation commit. Caller validation, task completion,
local integration into `v3`, and cloud handoff are separate responsibilities.

## Frozen lifecycle policy

The existing connection owner sends Close and owns transport I/O. No public
coordinator or temporary application session is introduced. Quiesce preserves
one absolute server deadline, stops upgrade admission, and starts a reserved
Close with code **1001**, reason **server drain**. A committed pending 101 flushes
before frame output; an upgrade losing the quiesce race remains ordinary HTTP.
An offered partial data frame remains pinned; unoffered data is discarded so
Close can use reserved control capacity without corrupting the wire.

The first successfully encoded application, peer, or drain Close transition
publishes `closing_since` under the session mutex. Duplicate close requests,
notifications, promotion after 101, empty output and successful writes cannot
slide this anchor. The independent output interval/write-idle contract from
TASK-122 remains intact. The watchdog selects the nearest applicable close,
handshake, write, and absolute drain deadlines and rechecks stale timers.
Terminal cancellation creates no fresh close-handshake anchor.

Local Close timeout reports `timeout` / **WebSocket Close timeout** and releases
only its owning connection. Global expiry reports `timeout` /
**WebSocket drain deadline**, with one shared scope claim before callbacks can
unwind counts. Peer Close metadata is preferred when available; otherwise
local metadata is retained. A clean peer exchange reports success/clean.
Terminal status and the callback are sticky; parked receive and writable
operations wake once. Controlled Close allocation failure terminalizes with
`limit_exceeded` / **Close failed**, without escaping nonthrowing lifecycle
callers or publishing a handshake anchor.

Drain expiry is shared by watchdog enforcement, ticket waiting and last-unit
leave. It captures the pre-cancel live count once; a late waiter still observes
`deadline_expired` after all units unwind. Completion before the deadline stays
completed. Handler initiation remains nonblocking and waiting on one's counted
work returns `would_deadlock`. Dropping the ticket does not disable deadlines.
The application retains its session through terminal notification.

## Doubt closures and TDD

| Attempt to refute the policy | Executed evidence |
|---|---|
| Global timeout disappears as callbacks remove counts | `drain_scope`: last leave after deadline, cancellation removing every unit, synthetic exact-boundary external expiry, last leave before deadline |
| Drain closes transport without sending Close | Native `websocket_drain` real wire/peer reply, pending-101 Close ordering; raw and independent server-initiated Close |
| No ticket means no deadline | Dropped-ticket native global timeout and external `DRAIN-DROP` |
| Observer delay or activity slides the first Close | `websocket_progress` atomic snapshot and duplicate initiation; engine empty-output, delayed observer, successful-write, prior-app-Close/stale-timer and pending-101 tests |
| One peer's failure stops unrelated connections | Local Close timeout leaves two WebSockets and an HTTP health request usable |
| Cancel/EOF/peer/app/drain races overwrite terminal state | `websocket_drain_race`: parked receive+writable, concurrent terminal events, one callback, stable status; allocation failure |
| Control starves behind offered output | Partial-frame/reserved-capacity driver assertions; independent real socket backpressure and ordered 64 MiB replay |
| Draining HTTP response loses its tail | Each external drain scenario preserves `/slow` body `oldbody` across initiation |
| Callback, observer or resume registration dangles | Weak engine expiry; reentrant progress and session race tests; inherited actual-API `task_resume_signal_lifetime` replay plus fresh native sanitizer gates below |
| Handler deadlocks waiting for itself | `/drain-handler` observes `would_deadlock`, retains session through clean Close; inherited ticket-outlives-server/drain-scope tests |

Meaningful RED receipts: `scope-red.log` failed five new sticky-expiry checks
(9 tests / 43 checks); `engine-quiesce-red.log` failed the two new Close/output
checks (12 / 87); `drain-wire-red.log` failed missing server Close (1 / 8);
`close-deadline-red.log` failed four new quiesce/fixed-anchor checks (13 / 93).
`driver-red.log` records the missing driver method/timestamp compile failure.
The initial attempted external RED was invalid: its fixture prefix expected a
trailing space for an empty subprotocol. It is not claimed as a feature RED.

Pre-guard focused checks include scope 11/54, progress 4/43, drain 3/29,
upgrade 16/113 and drain race 3/22. Post-guard upgrade replay is 17/117. The last upgrade/race additions were executed
by the full required suite after earlier focused receipts (15/106 and 2/16).
Resume lifetime is 1/8, resume signal 11/134, task race 4/2002 and session race
9/2008. Counts mean tests/checks, with zero failing checks in those pre-guard receipts; they are not final post-guard validation.

## Commands and current-tree gates

Configure: `../configure --enable-doxygen-doc CPPFLAGS=-I/opt/homebrew/include
LDFLAGS=-L/opt/homebrew/lib CXXFLAGS='-std=c++20 -O0 -g'`.
The first restricted configure produced invalid libtool command batching
metadata after denied `sysctl`. The authorized rerun recorded
`max_cmd_len=786432`; no source change was made for that environment issue.
The overloaded intermediate two-job test build was interrupted through its
inspected task-owned make supervisor; its receipt is retained.
Final native and test builds run serially; subsequent supervisors use
`nice -n15`, `-j1`, and no overlapping build/test/client/sanitizer workloads.

The initial `make check -j1` in `final-full-check.log` **failed**:
218 total, 217 pass, 0 skip, 1 fail, 0 error. Ordinary HTTP
`eof_before_request_closes` hit its unchanged ten-second bound. An isolated
original engine replay repeated the failure. No timeout relaxation or skip was
made. A current-archive single-case probe and a pinned current-dylib full-source
probe passed; an exact configured rebuild of the original engine test then
passed 36 tests / 207 checks, EOF in 0.7 ms. Source/header/object/executable and
library hashes before/after are retained. Timestamp evidence does not prove a
stale layout or production cause, so the earlier failures remain unresolved
old-executable observations; no speculative EOF patch is claimed.
The invocation journal is `.debug/eof-close-1232026/journal.md`.
A stalled LLDB diagnostic was stopped after inspecting only its task-owned
debugger/test/debugserver tree; exit and preserved receipt are recorded there.

The final independent command was
`_task123-receipts/client-env/bin/python test/integ/native_websocket_client.py
_task123-build/test/native_websocket_fixture --independent`.
`final-independent-client.log` records actual **websockets 15.0.1**, clean Close,
raw no-peer-response deadline, late/drop ticket, retained handler, local
isolation, handshake/frame ordering, HTTP preservation and backpressure PASS.

Final runtime, static/Doxygen, native audit and sanitizer outcomes follow below. The failed initial make command is never called a pass.

## Pre-guard static baseline comparison

Whole-source complexity and duplication were compared with an exact archive of
base `e1fb7cb4f812e476302bde7d624fc30f8c66ad46`, rather than an older task checkout.
The four inherited complexity functions remain: request dispatch 11, server
listen 11, peer pattern validation 15 and socket accept 11. Server listen's line
number shifts by one because of its standard include. At those checks there was no new offender.
Both trees contain the same two duplication groups: body_reader/response_writer
144 tokens / 43 lines; task.hpp 133 tokens / 26 lines. No new group is added.
Changed-path cpplint, file-size and diff whitespace checks must pass.

REQ-013 maps to one terminal reason/callback and race tests; REQ-031 to
nonblocking initiation/handler would-deadlock; REQ-032 to fixed deadlines,
sticky tickets and preserved HTTP response; REQ-033 to Close code/reason,
local/global timeout semantics and actual wire/client checks. TASK-128 remains
the later broader parity/interoperability task.

## Pre-guard segmented gate results

`final-runtime-check-tests.log`: `make -C _task123-build/test check-TESTS -j1`
passed **218/218**, zero skip/fail/error, after the fresh original EOF rebuild.
`final-check-local.log`: `make -C _task123-build check-local -j1` passed public
and private header boundaries, examples, lint helpers, native platform-only
linkage, README/release/hook documentation, staged install and header hygiene.
Doxygen actually ran and reported zero substantive warnings.

Fresh ASan/UBSan caught a new progress-test callback fixture error: `observed`
was declared after its callback-owning driver, so teardown wrote to expired
stack storage. `websocket_progress-red.log` retains the stack-use-after-scope
WRITE9 report. The minimal test-only repair moves storage before the driver.
No production change was made. The failed test process and its supervisors
were inspected, stopped and verified gone; report symbolization had stalled.
`inputs-post-testfix.json` verifies the only fingerprint change is that test.
The fresh native archive and five already-passing unchanged consumers are
retained; the affected progress consumer and remaining resume/lifetime/task
consumers are freshly rebuilt. Tests use `symbolize=0` and UBSan
`halt_on_error=1`; preserved logs and source/archive hashes distinguish the RED
from final evidence. This follows the same fixture lifetime ordering enforced
by TASK-122's earlier observer regression; it is not a production lifetime fix.

`final-asan-post-testfix.log` plus the five unchanged earlier consumer logs
pass all nine selected consumers. The newly rebuilt native fixture passed
`final-asan-client.log`: local timeout isolation, handler-safe drain,
raw clean/late/drop-ticket drain, independent server-initiated Close and
preserved HTTP response tail. Every process exited zero with empty stderr;
UBSan recovery cannot hide diagnostics. Client replay script/source SHA-256
hashes are in `client-replay-source-hashes.json`. The instrumented native
archive, executable hashes and exact flags are in `inputs.json`,
`inputs-post-testfix.json` and `PASS.json`.

Supported macOS/arm64 TSan then found a test-framework counter race in the
new drain deadline test. `atos` resolved worker/main writes to the two
concurrent `LT_CHECK` calls (`tsan-drain-red-symbols.log`); littletest's success
counter is non-atomic. Both deadline handlers now publish their receive outcome
in an atomic declared before the owner, and the test thread asserts it after
engine completion. No framework or production patch is made.
`websocket_drain-red.log` retains the failed TSan report. The disturbed ASan
drain consumer was rebuilt and passed 3/29; unchanged archive/consumer reuse is
recorded in `inputs-post-counterfix.json`. TSan uses the same fresh native
archive with newly built current consumers; diagnostic symbolization is disabled
and errors halt execution. Both actual failure observations remain in receipts.

The actual TSan fixture client exposed a production race after ownership
transfer: `suspension_deadline_locked` still read the HTTP exchange flag while
`exchange::upgrade` cleared it after sink commitment. The frozen base has the
identical exchange header, accessor and store (`timer-race-base-equivalence.json`).
The private watchdog now stops consulting HTTP suspension once phase becomes
pending-101 or WebSocket. Genuine HTTP suspension remains an active candidate;
write-idle, fixed Close and absolute drain timers retain their existing policy.
This is a timer ownership correction at the exercised upgrade/drain seam,
without changing public exchange synchronization or APIs.

`timer-ownership-red.log` deterministically failed the two transferred-phase
checks (17 tests / 117 checks, 115 success / 2 failure) while preserving the
ordinary-HTTP suspension check. The production repair adds one phase guard
before exchange reads. The final native sanitizer rebuild recompiles only
`connection_engine.cpp`; exact current source/compiler/flag hashes and all 24
retained object hashes are checked before rearchive. Required consumers and
fixture are relinked against the new current archive. Original failed TSan
fixture evidence is `tsan-client-timer-red.log`.

`timer-ownership-green-replay.log` passes **17/117**. The first full-suite GREEN
attempt passed the new regression but encountered two unchanged ordinary-HTTP
stop/EOF bounds in quiesce-before-upgrade; refusal itself was observed. That
failure is preserved in `timer-ownership-green.log`, with the exact successful
replay and no timeout relaxation or unrelated production repair.

The subsequent current TSan upgrade run passed cases 1–15 but caught the
same inherited suspension flag race in the ordinary HTTP refusal path:
`exchange::respond` clears the flag while the watchdog reads it. The phase
guard preserves genuine HTTP timers and cannot cover this HTTP case.
`http1_websocket_upgrade-http-suspension-red.log` and
`tsan-http-suspension-red-symbols.log` preserve the failure; no final race-clean
or completed-task claim is made until this required case is causally resolved.

## Committed cloud checkpoint: incomplete validation

This checkpoint preserves the implementation, deterministic timer ownership
regression and minimal private phase guard. **TASK-123 remains In Progress.**
The 218/218 runtime pass, check-local/Doxygen pass and ASan fixture pass above
predate the production phase guard and must not be presented as final-source
validation. The post-guard normal upgrade replay passes 17/117, but current
TSan stops in the HTTP refusal case on the inherited suspension flag race.
The nine-consumer TSan PASS manifest from before the guard is likewise not a
final-source pass; `inputs-final-timerfix.json` and the failed final logs record
the current native archive and preserved 24-object reuse proof.

No broader atomic flag/header repair, further builds, runtime gates or lint
runs are performed after this blocker. The executor was instructed to commit
this reversible checkpoint for cloud continuation, without merging or marking
the task Complete. Source and generated evidence are retained; no binaries,
keys or capabilities are committed. The caller owns the next repair scope,
final-source checks, validation and any eventual integration into `v3`.
