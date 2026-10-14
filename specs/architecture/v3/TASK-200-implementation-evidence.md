# TASK-200 implementation evidence

TASK-200 is ready for caller-owned Groundwork validation/finalization. Its card and the global index remain **In Progress**. No commit, staging, validation agent, merge, worktree operation or TASK-185 edit was performed during implementation.

Registered runner identity: `/home/etr/progs/libhttpserver/.worktrees/TASK-200`, `task/TASK-200`, base `v3` at `4604fa2fcd29ea32f7fd62fbdf21ee15d7f217ee`. The starting worktree was clean and registered on that branch, at the same commit as `v3`. The identity helper invoked inside this checkout proposed a nested worktree; the primary checkout helper requested monorepo selection. The caller's explicit registered identity took precedence; neither helper result caused a workspace change.

The [machine receipt](TASK-200-implementation-receipt.json) records exact commands, environments, exits, case/check/skip counts, source identities, staged-header identities and log hashes. Raw artifacts are under `build/task200-implementation/` in this worktree. The invocation-specific debug journal is `.debug/timer-executor-teardown-MHa1Mq/journal.md`; archive both directories before task cleanup.

## Immutable historical inputs

Read from the primary checkout, with no mutation:

| Receipt | SHA-256 |
| --- | --- |
| `/home/etr/progs/libhttpserver/build/task187-evidence/implementation-ad76a561/build/task187-implementation/tsan-off/extra-task-race-failure.log` | `f6f450322f88b5d34d6576b5d5d7321f2e80eee1288e6a89e2e478aadc9de147` |
| Same archive's `tsan-off/focused-check.log` | `c6d9e4809927f08c432ea12cbbbf4c4f514808b20e19c3fe843573d8aae17147` |
| Same archive's `base-task-race-identities.json` | `95414ffdf997e64fa998ecfe0cc004b8b0adc70d7197307742d6f7d7331369a6` |

The retained failure reports main-thread `manual_executor` destruction / `pthread_cond_destroy` against timer-thread `manual_executor::post` / `pthread_cond_signal`, exit **66**. The preceding focused selection passed seven other programs. The unchanged base comparison passed four cases and 2002 checks; that isolated pass did not resolve the intermittent finding. A fresh normal baseline here also passed four cases / 2002 checks / zero skips, exit 0.

## Demonstrated boundary and ownership

The regression interposes the actual queue-mutex unlock and `std::condition_variable::notify_one` call using Linux/libstdc++ linker wrapping. It discovers the queue mutex by observing `pending()` unlock, then gates only foreign producers unlocking that mutex. The shipped executor and coroutine/timer code execute unchanged during RED. No hook, constructor, global test state or macro enters a production header.

The controlled timeline is:

1. The actual timer callback wins the resume-node claim and posts the actual coroutine resumption.
2. The queue item is inserted; the producer completes the real queue unlock and pauses immediately after it.
3. The owner drains that item. The coroutine returns the checked timeout outcome exactly once, its completion callback runs on the borrowed executor, and `run_pending()` returns after frame destruction.
4. The owner attempts retirement and invokes losing signal/cancel triggers. No further queue item appears.
5. The producer is released. Unchanged production executes a real executor notification **after** the retirement attempt. GREEN executes none after that attempt.
6. A callback on the same serial timer thread acknowledges producer/callback exit before executor and probe storage are actually destroyed.

The signal-winning and cancel-winning variants first gate the real timer service, arm a zero deadline, publish from an explicit producer thread, then observe the same retirement boundary. Cleanup releases and joins that producer and releases the timer gate. A serial timer fence acknowledges processing of the losing cancelled deadline. Both variants observe exactly one publication, one checked result, correct executor affinity and no work/continuation after frame retirement.

This proves the **post tail** hypothesis: queue publication allowed completion to become observable before the final executor-member access. The late losing-producer hypothesis is excluded for these completed one-shot waits by the real cancelled deadline, repeated losing triggers and the publication/work counts. The old six-millisecond grace period did not acknowledge either producer completion or executor access and could not establish safe destruction.

RED keeps the executor alive until all producers exit, even when checks fail; it observes the unsafe ownership boundary without intentionally inducing undefined destruction. All assertions follow producer cleanup. Atomic gates establish ordering; five-second limits only diagnose deadlock. A timeout still releases the producer. Signal threads are joined, and timer fences retain captured state until the callback has exited.

## Behavioral RED and GREEN

The same focused regression command was run before and after the production repair:

```sh
/usr/bin/clang++-18 -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
  -DLHS_TEST_POST_TAIL_WRAP -DLHS_TEST_POST_TAIL_ONLY -Isrc -Itest \
  test/unit/task_race_test.cpp \
  -Wl,--wrap=pthread_mutex_unlock \
  -Wl,--wrap=_ZNSt18condition_variable10notify_oneEv \
  -o build/task200-implementation/{red,green}-regression
build/task200-implementation/{red,green}-regression
```

The brace alternatives identify the two separate invocations, not a single compiler command; executed paths are also recorded in the journal.

| Version | Cases | Checks | Failures | Skips | Exit |
| --- | --- | --- | --- | --- | --- |
| RED, unchanged executor | 2 | 35 | 3 | 0 | 3 |
| GREEN, repaired executor | 2 | 35 | 0 | 0 | 0 |

All three RED failures are `late_notifications == 0`, observed **1** instead: timeout, signal and cancellation each demonstrate the same tail. After refactoring the timer fences to retain their promise by shared ownership through callback return, the final regression was also compiled against the unchanged executor extracted from the base commit (`red-include/httpserver/concurrency/executor.hpp`): `red-replay` again exited 3 with two cases / 35 checks / three identical failures. Final GREEN again exited 0 with 35 successes. All six configured lanes were rerun after that fixture refinement. Initial instrumentation wrapping `pthread_cond_signal` did not intercept the shared libstdc++ notification and was rejected as a discriminator. The corrected full RED additionally encountered the existing scheduler-dependent stress assertion demanding both outcome classes. Focused RED separated the three causal failures from that unrelated count.

## Repair and contract implications

`manual_executor::post` now notifies **inside its existing queue lock**, before unlock makes the work available to a consumer. Its final executor access precedes publication. This is a one-line relocation plus an ownership comment. Retaining the unused condition-variable member preserves the public class layout; removing it would unnecessarily change layout. There is no public signature, constructor, shutdown contract or ownership change.

Timer cancellation still makes pending entries inert; it does not wait for already-running callbacks. Callback waiting is unnecessary for the demonstrated notification tail and would introduce self-cancellation/inline-resumption lock-order concerns. Frame witnesses, executor lifetime leases, borrowed executor references, executor-affine resumption, exactly-once claims and typed outcomes remain unchanged. Borrowing does not promise automatic ownership of arbitrary application executors or safe destruction with unrelated posters still active.

The old timeout stress family retires without its six-millisecond teardown sleep. It explicitly seeds one already-signalled wait and one genuine deadline-only wait, preserving both outcome checks and **198** real deadline-versus-signal races. Scheduler luck no longer determines whether both outcome classes receive coverage. The two deterministic regressions also run in the ordinary `task_race` target under **TSan**, outside the allocator-hook exclusion.

`consumer_v3_timed_resume` is registered/distributed through Automake. It uses only public concurrency headers, exercises real timed completion and signal/cancellation winners, checks typed result, affinity and exactly-once completion, repeats losing triggers and retires without a grace sleep. Existing executor and umbrella consumers retain their header-hygiene coverage.

The linker probe is available on Linux/libstdc++; `configure` enables only the test target's flags when that library is detected. Other platforms/standard libraries report the two probe cases as named skips; equivalent nonlocal instrumentation is CI/v3-PR owned. All required local lanes execute both regressions with **zero race-case skips**. No production dependency or race suppression was introduced.

## Required local verification

Toolchain: Ubuntu clang **18.1.3**, C++20. Reused TLS provider: **OpenSSL 3.5.9 29 Sep 2026**; it was not rebuilt. All six fresh out-of-tree lanes were bootstrapped/configured from this worktree using `/usr/bin/clang-18`, `/usr/bin/clang++-18` and common compilation flags:

```text
-std=c++20 -pthread -O1 -g -fno-omit-frame-pointer
```

Normal lanes add no sanitizer. ASan/UBSan lanes add `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fsanitize-address-use-after-scope`. TSan lanes add `-fsanitize=thread -DLHS_SANITIZER_OWNS_OPERATOR_NEW`. Link flags retain instrumentation and omit language/define switches needed only for compilation. TLS-off uses `--disable-v3-tls`; TLS-on uses `--enable-v3-tls` with:

```text
V3_TLS_CFLAGS=-I/home/etr/progs/libhttpserver/build/tooling/provider/include
V3_TLS_LIBS=-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto
```

Exact lane commands and environments are retained by the task-local `build/task200-implementation/run-lanes.py` and each lane's `results.json`. The common focused check is:

```sh
tests='task_executor task_core task_cancellation task_resume_signal task_resume_signal_lifetime task_race task_stop_inside_handler consumer_v3_executor consumer_v3_concurrency v3_header_hygiene consumer_v3_timed_resume'
make -j2 -C "$lane/test" check \
  TESTS="$tests" check_PROGRAMS="$tests" noinst_PROGRAMS=
make -C "$lane/src" install-nobase_includeHEADERS prefix="$lane/stage"
```

Each lane separately compiles and executes `consumer_v3_executor.cpp`, `consumer_v3_concurrency.cpp` and `consumer_v3_timed_resume.cpp` with that lane's flags and **only** `-I"$lane/stage/include"`. No private/source includes, `HTTPSERVER_COMPILATION`, library configuration macro or library linkage is used. All staged concurrency headers match source SHA-256 byte-for-byte; their exact identities are in the machine receipt.

Sanitizer builds/executions ran outside the ptrace sandbox. Diagnostics stayed enabled:

```text
ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
TSAN_OPTIONS=halt_on_error=1:suppressions=<worktree>/test/tsan.supp
```

Suppression file SHA-256 remains `bef8512659b644216f9b4ad3640fd77cf911a11c3e9899f8f5ff36b32f7aee85`.

| Lane | Programs | Harness cases / checks / skips | task_race cases / checks / skips | Staged consumers | All step exits |
| --- | --- | --- | --- | --- | --- |
| normal-off | 11/11 | 59 / 5819 / 0 | 6 / 2037 / 0 | 3/3 | 0 |
| normal-on | 11/11 | 59 / 5819 / 0 | 6 / 2037 / 0 | 3/3 | 0 |
| asan-ubsan-off | 11/11 | 59 / 5819 / 0 | 6 / 2037 / 0 | 3/3 | 0 |
| asan-ubsan-on | 11/11 | 59 / 5819 / 0 | 6 / 2037 / 0 | 3/3 | 0 |
| tsan-off | 11/11 | 59 / 5807 / 1 | 6 / 2037 / 0 | 3/3 | 0 |
| tsan-on | 11/11 | 59 / 5807 / 1 | 6 / 2037 / 0 | 3/3 | 0 |

Harness counts exclude the assertion-free consumer executables; program totals include them. The one existing TSan skip per lane is `task_resume_signal_lifetime`'s allocator-interposition case, unchanged from TASK-186. That suite executes 1800 active checks under TSan and 1812 under normal/ASan. No required local check is waived, and no new local race exclusion exists.

Changed C++ files pass `cpplint`; bootstrap validates the Autotools changes. `git diff --check` passes. Every lane's existing `lint-littletest-skip-exit-code` passes, pinning all-skip exit 77, mixed pass/skip exit 0, and failure-plus-skip failure count. The existing intentional self-move test emits clang's `-Wself-move` warning; changed files produce no new warning. Initial normal configure attempts erroneously passed `-std=c++20` to a C link probe and exited 77; initial logs were preserved before correcting task-local link flags. No failing diagnostic was replaced by a retry, delay or suppression.

## Requirement mapping and handoff

- PRD-V3N-REQ-024 / DR-V3-003: real timed application waits, signal resumption, captured executor affinity and preserved coroutine scheduling ABI.
- PRD-V3N-REQ-025: deterministic cancellation winner, losing deadlines, existing cancellation/lifetime suites and repeated terminal triggers.
- PRD-V3N-REQ-031 / DR-V3-008: existing `task_stop_inside_handler` runs in every lane; no synchronous callback-retirement wait was introduced.
- PRD-V3N-REQ-037: checked libhttpserver-defined resume outcomes and value envelopes; exceptional/non-value completions fail the regressions and public consumer.
- DR-V3-006: demonstrated timer/post/owner cleanup boundary, exactly-once claims and safe retirement. Protocol state and budgets are unaffected.

BSD, macOS, Windows and other nonlocal backend checks were **not executed**. They remain owned by CI and the v3 PR under AGENTS.md. Local Groundwork validation/finalization, merge and cleanup remain caller-owned; TASK-185 remains unchanged and blocked until this task's later phases finish.
