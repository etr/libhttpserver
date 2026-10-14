# TASK-200 validation recovery

The critical fixture-lifetime finding is locally repaired. The runner must retry
validation; this record does not claim review approval or task completion. TASK-200
remains In Progress on `task/TASK-200`. Changes are unstaged and uncommitted for the
runner-owned commit flow. No Groundwork skill, publication, merge, or next lifecycle
step was invoked.

## Cause and repair

`manual_executor_post_probe.hpp` previously published a raw pointer to stack
storage. Every intercepted mutex unlock read that pointer before deciding whether
its mutex matched. An unrelated timer-service reader could retain it beyond the
owner's deactivation and scope exit. A serial timer callback promise acknowledges
earlier callback processing, but does not retire every unrelated wrapper reader.

Discovery now uses C++20 `atomic<shared_ptr<publication>>`. Each wrapper holds a
strong reference before its first dereference. The thread-local producer also
holds a strong reference across the post tail and notification wrapper; the
existing joined-thread and serial-timer cleanup paths clear it. The capture
pointer remains confined to its owner's synchronous mutex-discovery call and is
cleared before publishing the initialized probe. Probe mutex identity and owner
are immutable after publication, and the remaining shared fields are atomic.

The local libstdc++ implementation in
`/usr/include/c++/13/bits/shared_ptr_atomic.h` uses an atomic reference-count lock
bit for shared-pointer load/store. Its unlock uses atomic operations, and its
spin relaxation uses processor pause or scheduler yield
(`bits/atomic_wait.h`). These operations do not recursively call the wrapped
pthread mutex unlock. Both TSan lanes execute this protocol without diagnostics.
The helper is enabled only for the existing Linux/libstdc++ interposition target.
No production code, API, executor ownership contract, or suppression changed.

## Deterministic regression and preserved production discriminator

`probe_lookup_retains_retired_publication` arms a reader-local gate immediately
after wrapper discovery and before its first publication dereference. The reader
unlocks an unrelated mutex; while it is paused, the owner clears discovery and
drops its last strong reference. A weak reference proves the reader still owns
the publication. The fixture then releases and joins the reader and proves the
publication has been destroyed. Assertions follow thread cleanup, and the gate
storage remains alive through the join. Bounded waits diagnose stalled progress;
no sleep, retry, grace interval, or sanitizer suppression supplies ordering.

| Check | Exit | Cases / checks / failures / skips |
| --- | --- | --- |
| Reported bounded audit reproducer with raw stack discovery | 1 | ASan stack-use-after-return at its first dereference |
| New regression with raw discovery, ASan/UBSan | 1 | 3 / 39 / 1 / 0; retained-ownership assertion fails |
| Repaired fixture, ASan/nonrecovering UBSan | 0 | 3 / 39 / 0 / 0 |
| Repaired fixture against unchanged base executor | 3 | 3 / 39 / 3 / 0; exactly the original timeout, signal, and cancellation late-notification failures |

The new RED assertion uses a weak reference to observe retirement deterministically.
The weak reference keeps the allocation's control block available, so that RED
is a behavioral lifetime failure, not a claim of a second ASan memory diagnostic.
The separately reproduced original stack case confirms the unsafe dereference.
An initial sandboxed RED execution also reported the ptrace LeakSanitizer
limitation; the retained RED receipt is the subsequent outside-ptrace execution
with leak checks enabled. Sanitizers and existing suppression inputs were kept
unchanged.

## Local verification

All six existing configured lanes rebuilt `task_race` and reran the eleven focused
programs, staged public headers, compiled and executed the three installed
consumers, and passed the Littletest skip-exit gate. Toolchain is Ubuntu clang
18.1.3, C++20. Commands and environments are in the
[recovery receipt](TASK-200-recovery-receipt.json), including log, binary, and
current source hashes. Recovery artifacts and copies of the latest per-test logs
are under `build/task200-recovery/`; the configured implementation build trees
were reused. The tracked original implementation receipt remains historical;
its per-test paths in the reused trees now contain the recovery runs.

| Lane | Programs | Harness cases / checks / skips | task_race cases / checks / skips | Installed consumers |
| --- | --- | --- | --- | --- |
| normal-off | 11/11 | 60 / 5823 / 0 | 7 / 2041 / 0 | 3/3 |
| normal-on | 11/11 | 60 / 5823 / 0 | 7 / 2041 / 0 | 3/3 |
| asan-ubsan-off | 11/11 | 60 / 5823 / 0 | 7 / 2041 / 0 | 3/3 |
| asan-ubsan-on | 11/11 | 60 / 5823 / 0 | 7 / 2041 / 0 | 3/3 |
| tsan-off | 11/11 | 60 / 5811 / 1 | 7 / 2041 / 0 | 3/3 |
| tsan-on | 11/11 | 60 / 5811 / 1 | 7 / 2041 / 0 | 3/3 |

All steps exit 0. The existing TSan allocator-interposition case remains the sole
skip in each TSan lane; the new cleanup case and both production regressions run
without skips. ASan leak/stack checks and nonrecovering UBSan run outside ptrace.
`cpplint` for both changed C++ files and `git diff --check` pass. Suppression SHA-256
remains `bef8512659b644216f9b4ad3640fd77cf911a11c3e9899f8f5ff36b32f7aee85`.
Nonlocal platform evidence remains CI/v3-PR owned under AGENTS.md.
