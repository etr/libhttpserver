# TASK-186 implementation evidence

Implementation is ready for caller-owned validation; task status remains In Progress.
Base: `v3` at `eadf781d6f2c8def9150ebc6ac036b56f94ac956`.
Registered worktree and branch: `/home/etr/progs/libhttpserver/.worktrees/TASK-186`, `task/TASK-186` (shared worktree-identity helper verified both).
No production concurrency headers changed.

## Causes and RED/GREEN

Frozen evidence was read from TASK-185's `build/task185-final-native-audit-05/asan-ubsan-off/consumer_v3_concurrency.log` and `tsan-{off,on}/command-0002.log`. Its historical artifacts and acceptance status were preserved.

1. **Verified consumer closure lifetime error.** The immediately invoked captured coroutine lambda dies at the creation statement; its lazy body dereferences the expired closure during executor draining. The original ASan baseline reproduced `stack-use-after-scope`, exit 1. The stronger regression still using that closure also reproduced the fault, exit 1. Replacing only the temporary closure with a named coroutine taking `resume_signal` by value eliminated it. The signal lives in its coroutine frame. The regression checks lazy start, both suspended signal/cancel winners, an already-fired signal, shared copies, losing/repeated triggers, one completion and a drained queue. Every completion requires a value; `main()` returns the real check status. A temporary copy deliberately expecting `value_seen == -1` exits 1, proving failed checks reach the process result.
2. **Verified allocator ownership conflict.** The actual Clang18 TSan lifetime target fails to link because its global `operator new/delete` conflict with `libclang_rt.tsan_cxx`. The direct source link exits 1 and the configured make target exits 2 with those exact duplicate definitions. The ownership guard follows `websocket_drain_race_test.cpp`: explicit `LHS_SANITIZER_OWNS_OPERATOR_NEW`, GCC TSan, and Clang thread/memory/leak feature detection. Global overrides and all hook-only state are guarded together. The actual configured target and an automatic Clang TSan detection build without the explicit ownership define both link and execute successfully.

The original forced timeout-registration reclaim case remains active under normal and ASan/UBSan. Both signal and cancel winners assert the hook fired, one delivery, the correct result, waiter destruction and list removal (12 checks total). Timer initialization stays outside the injection window. This target contains no explicit allocation-failure test; its allocator throws only when real `malloc` fails. No unrelated `bad_alloc` acceptance was added, and other owner suites were not changed.

Only `published_waiter_can_die_during_timeout_registration` skips in sanitizer-owned lanes, with this reason: `sanitizer owns operator new; timer-allocation reclaim hook runs in normal and ASan/UBSan lanes`. The suite keeps two allocator-independent race cases active: 100 competing signal/cancel iterations and 200 suspended-frame retirement iterations covering both winners and both already-queued wakes and racing publication. Start gates use atomics, observations follow joins, and the retirement case proves no resumed body, retired witness, released node and empty waiter/queue state. There are no added sleeps or sanitizer suppressions.

## Executed commands

All commands ran from this worktree, using Ubuntu Clang **18.1.3**, C++20 and the retained optimization/debug/instrumentation flags. Task-local commands and full outputs are preserved under `build/task186-implementation`; this directory is ignored build evidence. The invocation-unique debug journal is `.debug/concurrency-consumer-lifetime-o9d1ucnm/journal.md`.

Consumer baseline, strengthened RED, and minimal-fix GREEN each used the corresponding then-current source and this command (distinct `baseline-consumer`, `red-consumer`, `green-consumer` outputs/logs):

```sh
clang++-18 -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -fsanitize-address-use-after-scope -Isrc \
  test/headers/consumer_v3_concurrency.cpp -o build/task186-implementation/green-consumer
ASAN_OPTIONS=detect_stack_use_after_return=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build/task186-implementation/green-consumer
```

Initial sandbox GREEN encountered LeakSanitizer's ptrace limitation. The successful GREEN and full local sanitizer matrix ran outside the ptrace sandbox, retaining leak and stack checking. This is an execution boundary, not an instrumentation exclusion.

Allocator direct RED and automatic-detection GREEN:

```sh
clang++-18 -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
  -fsanitize=thread -DLHS_SANITIZER_OWNS_OPERATOR_NEW -Isrc -Itest \
  test/unit/task_resume_signal_lifetime_test.cpp \
  -o build/task186-implementation/baseline-lifetime
# RED: duplicate global new/delete. Then build the actual configured target:
make -C build/task186-implementation/tsan-off/test task_resume_signal_lifetime
# RED before guard, GREEN after guard.
clang++-18 -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
  -fsanitize=thread -Isrc -Itest test/unit/task_resume_signal_lifetime_test.cpp \
  -o build/task186-implementation/automatic-tsan-lifetime
TSAN_OPTIONS="halt_on_error=1:suppressions=$PWD/test/tsan.supp" \
  build/task186-implementation/automatic-tsan-lifetime
```

The first direct lifetime attempt omitted `-Itest` and failed to find `littletest.hpp`; it is retained as setup-error evidence, not counted as RED. The corrected direct link and actual configured target establish RED.

Each of the six fresh out-of-tree lanes was configured with `CC=/usr/bin/clang-18`, `CXX=/usr/bin/clang++-18`, common `CXXFLAGS='-std=c++20 -pthread -O1 -g -fno-omit-frame-pointer'` and these additions in both compilation and link flags:

| Lane | Additional flags |
| --- | --- |
| normal | none |
| asan-ubsan | `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fsanitize-address-use-after-scope` |
| tsan | `-fsanitize=thread` (compilation also `-DLHS_SANITIZER_OWNS_OPERATOR_NEW`) |

TLS-off uses `--disable-v3-tls`. TLS-on uses `--enable-v3-tls` and:

```sh
V3_TLS_CFLAGS=-I/home/etr/progs/libhttpserver/build/tooling/provider/include
V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'
```

The reusable static provider reports **OpenSSL 3.5.9 29 Sep 2026**. It was not rebuilt. No protocol/provider suites were built. Exact configure commands/logs, selected compiler/provider environment and per-step exit codes are recorded in each lane's `configure.log`, `results.json`, and `results.focused.json`.

```sh
tests='task_executor task_core task_cancellation task_resume_signal task_resume_signal_lifetime task_race task_stop_inside_handler consumer_v3_executor consumer_v3_concurrency v3_header_hygiene'
make -j2 -C "$lane/test" check TESTS="$tests" check_PROGRAMS="$tests" noinst_PROGRAMS=
"$lane/test/task_resume_signal_lifetime"
make -C "$lane/src" install-nobase_includeHEADERS prefix="$lane/stage"
clang++-18 $flags -I"$lane/stage/include" \
  "$PWD/test/headers/consumer_v3_concurrency.cpp" -o "$lane/installed-consumer"
"$lane/installed-consumer"
```

`$lane` is the absolute path `build/task186-implementation/{normal,asan-ubsan,tsan}-{off,on}`; `$flags` is that lane's compilation flags. Installed consumer commands use only the staged public include path and standard toolchain paths, with no source/private includes, compilation/configuration macros, or libhttpserver linkage. `install-nobase_includeHEADERS` is the existing public header installation rule.

The plan's original narrow `make check` command also caused Automake to build unrelated `noinst_PROGRAMS` protocol fixtures via `check-am -> all-am`, failing before tests because `native_websocket_fixture` required an unbuilt v3core library. Initial logs/results for all six lanes remain preserved. Adding `noinst_PROGRAMS=` scopes the build to the same ten required tests; no required test or diagnostic was removed. Scoped executions are logged separately as `*.focused.log`.

All lanes ran with:

```sh
ASAN_OPTIONS=detect_stack_use_after_return=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
TSAN_OPTIONS="halt_on_error=1:suppressions=$PWD/test/tsan.supp"
```

`test/tsan.supp` is unchanged. TSan ran locally in TLS-off/on; it was not deferred to CI.

## Final local results

| Instrumentation | TLS | Selected programs passed | Lifetime cases / checks / skips | Staged consumer |
| --- | --- | --- | --- | --- |
| normal | off | 10/10 | 3 / 1812 / 0 | pass |
| normal | on | 10/10 | 3 / 1812 / 0 | pass |
| asan-ubsan | off | 10/10 | 3 / 1812 / 0 | pass |
| asan-ubsan | on | 10/10 | 3 / 1812 / 0 | pass |
| tsan | off | 10/10 | 3 / 1800 / 1 | pass |
| tsan | on | 10/10 | 3 / 1800 / 1 | pass |

All six configured lanes compiled and executed the ten selected programs (60 program passes total). No ASan/UBSan or TSan diagnostics occurred in successful runs. Automake reports each lifetime program as PASS even in TSan because two active cases pass and only the named hook case skips; direct case/check/skip counts above expose that boundary.

`cpplint test/headers/consumer_v3_concurrency.cpp test/unit/task_resume_signal_lifetime_test.cpp`, `make -C build/task186-implementation/normal-off lint-littletest-skip-exit-code`, and `git diff --check` pass. The skip-semantics gate verifies mixed pass+skip returns 0 and all-skip returns 77. No new source or reusable test script was introduced, so no test/distribution registration is needed.

The five staged concurrency headers match source byte-for-byte in all six installations (SHA-256):

- `concurrency.hpp`: `6358d1429a64c887fc173a99e0772d1608e51c5498bc018de33a84e079ae6224`
- `executor.hpp`: `0d3b841c43bbe4bad1fc4ff8ce59ee373773e84c3ee85ffbcc52111fe0b58bb5`
- `task.hpp`: `494a0d7a55e0efab67868ae86380223a1a463fc222233d1f6b7209028786556e`
- `cancellation.hpp`: `4e9ff791f53a102424385cb59c39c8af3d7a3402c067295134a81c84fa645f54`
- `resume_signal.hpp`: `8cb1a9019027d950ad5b80a8c7f93d6ddbbbc07652169a7d5d6b87ef8678580b`

## Requirement and decision traceability

- PRD-V3N-REQ-024/025, DR-V3-003: delayed public application resume owns signal lifetime, returns the checked resumed/cancelled outcome once, and keeps meaningful cancellation/retirement coverage under sanitizers.
- PRD-V3N-REQ-037: consumer and competing-terminal cases verify libhttpserver's typed resume outcome; exception/non-value completions fail.
- DR-V3-006: forced timer-registration reclaim and suspended-frame retirement prove cleanup at the owner boundary; no protocol state changes.
- PRD-V3N-REQ-031, DR-V3-008: existing task/cancellation/request-stop-inside-handler owner suites execute in every local lane, retaining handler-safe stop regression coverage.

BSD/macOS/Windows and other nonlocal checks were not run; they remain CI/v3-PR owned under AGENTS.md. Local checks are complete. Groundwork validation, final task completion, merge into `v3` and worktree cleanup remain caller-owned. No TASK-187 work, release publication, push or deployment occurred.

## Reusable project notes

Setup is `./bootstrap`, then out-of-tree `configure` and bounded `make -C <lane>/test check`. Public concurrency targets have empty `LDADD`; installed headers use `make -C <lane>/src install-nobase_includeHEADERS prefix=<stage>`. Scope `TESTS`, `check_PROGRAMS` and `noinst_PROGRAMS=` together to avoid unrelated fixture/library builds. The custom framework is `test/littletest.hpp`, with named cases in `test/unit/*_test.cpp` and standalone consumers in `test/headers`. Use `-Itest` for direct unit compiles. Run sanitizers outside the ptrace sandbox without disabling leak/stack checks. Clang18 TSan owns global new/delete; preserve only allocation-hook exclusions, leaving race cases active. No configured task-executor memory store was present; these notes and the unique journal carry the actionable project memory.
