# TASK-125 implementation evidence

The executor reused the registered `/Users/etr/progs/libhttpserver/.worktrees/TASK-125`
worktree on `task/TASK-125`, based on `v3` at
`5e1c6404a11165eb72b6eb1eef6daee7d4c448af`. Initial tracked/untracked status was
clean (the validated plan is ignored). The runner owns staging, commits,
validation, integration, and cleanup; the executor performed none of those.
TASK-125 remains In Progress pending caller validation/finalization.

## Implemented behavior

`server_options::loop()` selects managed (default) or external readiness.
External mode supports plaintext HTTP/1 and worker concurrency; malformed loop
values return `invalid_argument`, and TLS/HTTP2/HTTP3 selections return
`not_supported` through pure configuration validation before binding.
`native_server::readiness()` returns a stable, borrowed, server-owned driver or
null in managed mode. Before successful listen and after stop, it is inactive:
empty snapshots, no wake/deadline, and `invalid_state` dispatch. Stop unwinds
pending operations and joins workers without further host dispatch.

The existing poll backend implements the adapter; external mode starts no
internal polling thread. Socket steps, terminal claims, owner enqueue, and
ordered timer sweeps are shared with managed mode. Adapter code lives in
`io_external_adapter.cpp`; wake-source implementation lives in
`io_wake_source.cpp` to keep the platform header within its size ceiling.

Each socket lifetime has an identity independent of its descriptor. Snapshot
omission revokes the published generation; republishing cannot authorize an
old callback. Release/re-adopt uses a different lifetime, including actual
POSIX descriptor reuse. Identity/generation exhaustion fails without wrapping.
Dispatch validates identity and acquires a shared socket lifetime under the
registry mutex. That lifetime survives detached batches and defers physical
close until dispatch relinquishes it. Rearm checks the operation's original
lifetime against the current registration; a matching private connection ID
alone cannot revive old work. Accepted sockets are rejected/closed if listener
retirement or shutdown wins registration. Owner enqueue happens outside the
registry lock, including immediate rejection after close.

Snapshots prune retired registry records and own complete socket/wake/deadline
values, so repeated external connections do not accumulate tombstones. Timer deadlines are
actual pending deadlines, and expiry retains `(deadline, sequence)` ordering.
An RAII dispatch guard rejects overlap and recursion before consumption, and
host time must be nondecreasing. Dispatch processes readable data before close
or error teardown where socket results permit it.

Notification is separate from private wake-operation completion. Publishers
coalesce one token under the registry mutex; acknowledgement takes the same
mutex and performs one bounded read before processing events. A concurrent
mutation either precedes that acknowledgement or leaves a readable token
behind it. Dispatch has no trailing drain. Interrupted writes retry; only
actual would-block buffer fullness counts as an existing notification, and
hard signal/acknowledgement failures terminate work. Construction/nonblocking
setup failure leaves an invalid source for the typed pre-bind listen gate.
There is no external-mode idle polling fallback.

The public-header-only native example and its POSIX poll/Windows WSAPoll helper
retain pairs alongside native registrations and rebuild a complete set after
successful dispatch. Without a library deadline they wait indefinitely. The
optional seconds argument bounds an entire smoke invocation. Integration
fixtures use a fixed failure deadline and fail on reaching it before dispatch;
they never turn the failure limit into a periodic progress mechanism.

## TDD receipts and regression coverage

Receipts are in the ignored task-local `_build/task-125/` directory:

- `baseline-io-contract-authorized.log`: clean-driver baseline passed 41 tests,
  734 checks. An earlier sandbox run could not perform the TCP socket probes;
  it is not a passing baseline. The first broad baseline command overlapped
  introduction of the new declarations and was not treated as baseline proof.
- `red-public.log`: the source-only public consumer failed for missing
  `readiness()`, `loop()`, and `loop_mode` before their implementation.
- `red-adapter.log`: adapter tests failed compilation for the absent concrete
  external mode and enforcement state.
- `red-host.log`: the HTTP/1 host test failed for the absent native host helper.
- `red-exhaustion.log`: a real regression failed because an identity-exhausted
  registration left its unowned socket open. The registration factory now
  closes the handle on failure; `green-exhaustion.log` passed 16 tests / 336
  checks before the wake-source extraction and additional coverage.
- `red-retired-pruning.log`: repeated external release/snapshot cycles failed
  16 assertions because retired records accumulated without the managed polling
  thread's projection cleanup. External snapshots now prune those records;
  `green-retired-pruning.log` records the regression and complete adapter suite.

Adapter regressions use real sockets and manual/inline executors. They cover
pending-until-dispatch behavior; read/write/accept completion; masks, owned
snapshots, and usable wake handles; spurious/duplicate/all-false callbacks;
unknown keys, wrong generations, and omitted/republished registrations; all
16 old-event flag combinations after same-ID and actual descriptor reuse;
detached read and accept release/reopen before rearm; timer deadline boundaries,
ordering, cancellation races, and decreasing-time rejection before socket I/O;
work after snapshot and during mutex-controlled acknowledgement; publication
during a barrier-controlled in-flight dispatch; recursive and concurrent
dispatch rejection; physical close deferral during completion; shutdown and
hard wake failure; identity exhaustion; buffered read with combined close/error;
descriptor zero as a valid carrier; and bounded retired-registry cleanup. Private member-pointer access controls
otherwise unreachable scheduling boundaries without production test hooks.

Native-server tests also force wake construction failure in an isolated POSIX
child with a zero descriptor soft limit and verify typed failure before bind.
The child has a bounded wait and restores no shared process limits.

Public-API HTTP/1 integration covers GET, bounded POST, keep-alive/pipelining
with ordered response bodies, a deliberately gated worker response with
observed wake dispatch, header deadline expiry through empty dispatch, close
and reconnect with retained stale callbacks, and host-driven drain/stop with
registration removal.

## Local verification

Local platform: Darwin arm64, Apple Clang 21.0.0
(`arm64-apple-darwin25.3.0`). Autotools bootstrap and the examples-enabled C++20
VPATH configuration used:

```sh
../../configure --enable-examples --enable-doxygen-doc CXX=clang++ CXXFLAGS='-O0 -g0' CPPFLAGS='-I/opt/homebrew/include' LDFLAGS='-L/opt/homebrew/lib'
```

Builds and test runs used `-j1`. Runtime socket checks ran with authorized local
loopback access. Existing macOS linker warnings about `-bind_at_load` and
repeated `-lc++` remain; changed C++ code introduced no compiler warnings.

The focused 13-target command in `focused-final-check.log` passed 13/13 with no
failures/skips, including both external suites and managed backend/native HTTP/1
regressions. The complete local gate also includes source/installed public
header consumers, the native dependency audit, staged install layout/hygiene,
and zero-warning Doxygen generation.

After the registry-cleanup repair, `final-build.log` records a successful
serial examples-enabled build and `final-full-check.log` records the complete
`make -C _build/task-125 -j1 check`: 223/223 executables passed, with zero
failures, skips, or errors (exit 0). This final full run includes every focused
target against the final library and test source. The adapter suite passes
19 tests / 406 checks; public HTTP/1 integration passes 4 / 52; native server
lifecycle passes 17 / 76. Source/installed consumer checks, native dependency
auditing, staged installation/hygiene, and Doxygen checks pass in that same run.

The actual example was run against the final native core as:

```sh
_build/task-125/examples/v3_external_event_loop 3
curl --silent --show-error --fail --max-time 2 --header 'Connection: close' --write-out '\nSTATUS=%{http_code}\n' http://127.0.0.1:49179/hello
```

`example-smoke.log` records the example's printed port 49179, HTTP status 200,
exact body `hello from an external loop` followed by newline, curl exit 0, and
example exit 0 within its three-second invocation budget. A bounded subprocess
probe asserts those bytes/status/exit codes and terminates any failed child;
no example server remains running. The smoke runs the actual example binary,
not the integration fixture.

## Static checks and inherited debt

Changed-file `python3 -m cpplint --extensions=cpp,hpp --headers=hpp` passes.
`scripts/check-file-size.sh` passes without new exemptions; extraction of the
wake implementation repaired the temporary 505-SLOC platform header.
`git diff --check` passes.

The full complexity command still exits 1 for three unchanged baseline
functions: `dispatch_request` (CCN 11), `valid_peer_pattern` (CCN 15), and
`accept_one` (CCN 11). A `git archive HEAD src` baseline, measured with the same
Lizard tool, records those same offenders plus the old `impl::listen` (CCN 11).
This change reduces listen below the threshold and adds no offending function.
See `baseline-complexity.log` and `complexity.log`; the full complexity gate is
not claimed passed.

The full duplication command still exits 1 for the identical two baseline
clusters in `body_reader.hpp`/`response_writer.hpp` and the two task specializations
in `concurrency/task.hpp`. No changed/new code appears in the reported clusters.
Same-tool baseline/current receipts are `baseline-duplication.log` and
`duplication.log`. The full duplication gate is not claimed passed; inherited
cleanup remains a repository-owner concern outside TASK-125.

## Platform and workflow limits

Only the local macOS build/runtime/package checks were executed. Linux, BSD,
and native Windows/MSVC/MinGW runtime/consumer checks were not executed here;
Windows socket width and branch code are not native runtime proof. Per the
user's AGENTS policy, those checks belong to CI and the v3 PR and do not block
local completion. POSIX dup2, descriptor-zero, and descriptor-exhaustion probes
are explicitly POSIX coverage. Groundwork validation, runner-owned commit,
merge into v3, and worktree cleanup remain the caller's next phases.


## Validation repair iteration 1

The executor receipts above describe the original implementation. The current
repair adds transactional socket-direction and shared due-timer collection:
all potentially throwing vector growth finishes before registry erasure. Both
readable and writable collections finish before either is detached in an
external event. Managed dispatch uses the same collection helper, and timer
expiry preserves `(deadline, sequence)` ordering and owner enqueue outside the
registry mutex.

Eight bounded regressions now cover allocation failure in read/write/accept
collection, failure while collecting the second direction, due-timer allocation
failure, generation exhaustion after maximum-generation snapshot omission,
and real successful native accept followed by listener retirement or backend
close before registration. The accepted-socket cases split the existing native
accept/registration boundary using private member access, retain the listener
lease, and exercise actual teardown and production registration/completion
helpers; they add no scheduling hooks.

Iteration receipts are in the runner-owned validation findings directory under
`repair-iter1-*` names. `socket-probe-red.log` and `timer-probe-red.log` reproduce
the original one-of-two operation loss. `suite-red-authorized.log` records the
five allocation regressions failing before repair. `guards-mutation-red.log`
records new exhaustion and both accepted-socket tests failing when the existing
guards are temporarily removed; the original guards were restored exactly.
`socket-probe-green.log` and `timer-probe-green.log` both report two pending
operations before close, two completions by close, and both states terminal.

Focused final receipts, all with zero failures/skips:

- `suite-green.log`: external adapter, 27 tests / 491 checks.
- `managed-contract-green.log`: shared managed I/O contract, 41 / 734.
- `fake-timer-green.log`: fake backend/timer coverage, 14 / 2523.
- `native-server-green.log`: native server lifecycle, 17 / 76.

The focused runtimes used authorized local loopback access. An initial restricted
sandbox adapter run failed listener creation and is not passing verification.
Changed-file cpplint, `git diff --check`, and source file-size checks pass;
existing macOS linker warnings remain. The repair performed no staging,
commit, merge, or finalization. Final broad gates and acceptance remain
coordinator-owned; nonlocal platform checks remain assigned to CI and the v3 PR.

## Validation repair iteration 2

The first repair's two detachment loops raised `dispatch_event` to CCN 11.
A shared private `detach_batch_locked` now performs registry erasure for both
external dispatch and managed direction dispatch. Both external direction
collections still finish before either detachment call, and detachment remains
under the registry mutex. Dispatch, completion, and timer behavior are unchanged.

The unchanged complexity threshold now measures `dispatch_event` at CCN 9,
`detach_batch_locked` at CCN 2, `take_direction_locked` at CCN 1, and
`collect_direction_locked` at CCN 7. The full complexity command still exits 1
only for the three inherited offenders named above; it is not claimed passed.
Changed-file cpplint, source file-size, and `git diff --check` pass.

Focused regressions pass with zero failures/skips: external adapter 27 tests /
491 checks, managed I/O contract 41 / 734, and fake backend/timer 14 / 2523.
The native core and these three test executables were rebuilt before running.
Receipts are in the runner-owned validation findings directory under
`repair-iter2-*` names. This is a private refactor verified by the existing
allocation-failure and I/O regression coverage; no new tests or public contracts
were added. Final broad gates remain coordinator-owned.
