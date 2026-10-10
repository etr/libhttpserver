# TASK-187 implementation evidence

Implementation is committed for caller-owned Groundwork validation. Task status remains In Progress; validation, completion, merge and cleanup remain caller-owned.

Base branch: `v3`, SHA `200c12ee2ac0b166488d4a7261c1a83f5cb5ecdb`.
Identity helper returned the registered worktree `/home/etr/progs/libhttpserver/.worktrees/TASK-187` and branch `task/TASK-187`, used verbatim.
The invocation-unique debug journal is `.debug/response-multipart-lifetimes-wCYcZu/journal.md`.
Full commands, outputs, initial failures and final receipts are retained under `build/task187-implementation` in this worktree; preserve these before cleanup. TASK-185 and its frozen artifacts were only read.

## Verified causes and repairs

1. **Response fixture ownership.** `fake::byte_span("kept")` implicitly creates a temporary string. The counter-only keeper did not own its bytes. Fresh baseline ASan reproduced the retained `borrowed_destruction_releases_keeper_once` stack-use-after-scope. Strengthening the case with exact byte/length assertions reproduced it again before repair. The local `leased_payload` now owns both the immutable string and release counter, and the span and lease refer to that same owner. No production response API, body sink or borrowed-data copying changed.
2. **Multipart lazy-reference ownership.** Public `read_multipart(exchange&, const multipart_limits&, part_sink&)` previously saved the limits reference across initial suspension. The original temporary call in `read_disconnect_aborts_once` reproduced the retained stack-use-after-scope. New call-time mutation coverage failed six assertions, and an ordinary named helper returning an unstarted task with temporary custom limits independently reproduced ASan failure. A synchronous public wrapper now copies the four scalar budgets into a private by-value coroutine frame at invocation. Admission and decoding remain lazy. Exchange and part sink stay explicitly borrowed through task completion/destruction.

The public multipart signature and exported symbol are preserved, including the exact installed function-pointer assertion. The wrapper avoids a public by-value signature/symbol change; copying only inside the old lazy body was rejected because it would still dereference expired limits. The journal compares both interfaces on depth, locality and seam placement and records the adversarial doubts. Temporary-helper ASan, lvalue mutation, cancellation and staged symbol/type checks clear those doubts. Git revert provides rollback; no irreversible external state changed.

The registered route closure in `make_multipart_route` persists in the registry through the existing route tests. Its helper borrows that live closure's limits/handler. The original and helper-temporary multipart failures reproduce through the direct public read boundary without any coroutine lambda, separating the reference cause from closure ownership. Added helper functions returning tasks are ordinary functions; no temporary captured coroutine lambda was introduced.

## Regression behavior

- Borrowed response tests cover delayed executor start after dropping the application owner, replay through a definition copy, parked partial writes under a four-byte queue bound, explicit cancellation and disconnect. They require a value result, exact bytes/counts, one completion, no later output after terminal handling, and one keeper release only after the final legitimate definition disappears. Existing file/pipe transfers and concurrent replay tests remain active.
- Multipart tests keep the original temporary syntax, exercise temporary custom limits after the helper returns, mutate lvalue limits before starting, and assert individual names/part bytes (`a=1`, `file=payload`), completed counts and no abort for completed parts. Parked disconnect/cancel cases require typed value completion, one delivery, one begun-part abort, zero completed parts and no response output, including repeated terminal notifications. Existing malformed/budget/file/decoder/corpus checks remain active.
- The new registered/distributed `consumer_v3_multipart_runtime` uses installed public headers only. It feeds a bounded body through the public body-source seam and real multipart decoder; both lvalue mutation and temporary-helper scenarios check one completion, exact field name/value and one successful part with zero aborts. The existing vocabulary consumer remains link-free and now asserts the exact unchanged read signature.
- Migration documentation states call-time limits ownership and that a response lease must own the actual borrowed storage.

## RED evidence and counts

All ASan runs use leak and stack checking; no lifetime suppression was added.

| Receipt | Result |
| --- | --- |
| Fresh original two-target baseline | scoped make exit 2, 0/2 passes; each direct target exit 1 with its retained ASan stack-use-after-scope |
| Strengthened response regression | exit 1, ASan abort at case 8 |
| Strengthened multipart regression | six failing checks in lvalue-snapshot case 9, followed by ASan abort in temporary-helper case 10; exit 1 |
| Staged installed lvalue snapshot consumer before fix | compile/link exit 0, runtime exit 1 |
| Separate staged temporary-only consumer before fix | compile/link exit 0, runtime exit 1 with ASan stack-use-after-scope |
| Corrected response/multipart targets | 30 cases / 304 checks and 14 cases / 178 checks, respectively; all six final lanes exit 0, zero failures/skips in these targets |

ASan aborts do not print final harness totals; partial RED totals are not claimed. `red-response_sources_test.cpp`, `red-forms_multipart_route_test.cpp`, `red-*.log`, and `red-results.json` preserve the source state and receipts. The original installed shared library is retained as `asan-ubsan-off/baseline-libhttpserver.so.3.0.0` with hash and symbol listing. Frozen TASK-185 logs have SHA256 `c95c2c83da5c3dc147dad1471d4ae563aebc4adbfe10b23a28629fb66e3779f9` (response) and `07561b7db5c253772962345b1c27fdbdfd425cbec05529c950f110a94e8cc811` (multipart).

## Executed local commands

Toolchain: Ubuntu Clang **18.1.3**, C++20. Reused provider: `/home/etr/progs/libhttpserver/build/tooling/provider`, **OpenSSL 3.5.9 29 Sep 2026**. Compiler/provider identity outputs are retained. Six fresh out-of-tree lanes were configured with `CC=/usr/bin/clang-18`, `CXX=/usr/bin/clang++-18`, `--disable-examples` and TLS off/on. No provider rebuild or new production dependency was introduced.

Common compilation flags:

```sh
-std=c++20 -pthread -O1 -g -fno-omit-frame-pointer
```

ASan/UBSan compilation/link additions:

```sh
-fsanitize=address,undefined -fno-sanitize-recover=undefined -fsanitize-address-use-after-scope
```

TSan compilation/link additions are `-fsanitize=thread`; compilation also uses the established `-DLHS_SANITIZER_OWNS_OPERATOR_NEW`. TLS-off uses `--disable-v3-tls`. TLS-on uses `--enable-v3-tls` and:

```sh
V3_TLS_CFLAGS=-I/home/etr/progs/libhttpserver/build/tooling/provider/include
V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'
```

The task-local `run_lane.py` records exact configure/library/test invocations and exit codes. Normal and ASan/UBSan final selection:

```sh
tests='response_sources response_definition response_writer body_reader multipart_decoder forms_multipart forms_multipart_route forms_multipart_files forms_multipart_corpus task_cancellation task_resume_signal_lifetime consumer_v3_response_sources consumer_v3_response_definition consumer_v3_response_writer consumer_v3_multipart consumer_v3_multipart_runtime v3_header_hygiene'
make -j2 -C "$lane/src"
make -j2 -C "$lane/test" check TESTS="$tests" check_PROGRAMS="$tests" noinst_PROGRAMS=
```

TSan's affected owner selection is:

```sh
tests='response_sources response_definition response_writer body_reader forms_multipart_route task_cancellation task_resume_signal_lifetime'
```

All sanitizer execution was outside the ptrace sandbox with:

```sh
ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
TSAN_OPTIONS="halt_on_error=1:suppressions=$PWD/test/tsan.supp"
```

`test/tsan.supp` is unchanged (SHA256 `bef8512659b644216f9b4ad3640fd77cf911a11c3e9899f8f5ff36b32f7aee85`). Initial sandbox configure exit 77 was LeakSanitizer's ptrace limitation, then outside-sandbox configure passed with all diagnostics retained. Initial TLS-on ASan make exited 2 because its generated Makefile predated the new consumer rule; `automake --add-missing` and each lane's `./config.status test/Makefile` resolved registration. These setup failures and their logs remain preserved. The final TLS-on check executes every required target.

## Final affected matrix

Every final library build, scoped make and direct response/multipart/lifetime execution exits 0. Exact per-program counts are also in `final-case-counts.json`.

| Instrumentation | TLS | Programs passed | Harness cases / checks / skips | Staged consumers |
| --- | --- | --- | --- | --- |
| normal | off | 17/17 | 144 / 4403 / 0 | 5/5 |
| normal | on | 17/17 | 144 / 4403 / 0 | 5/5 |
| asan-ubsan | off | 17/17 | 144 / 4403 / 0 | 5/5 |
| asan-ubsan | on | 17/17 | 144 / 4403 / 0 | 5/5 |
| tsan | off | 7/7 | 123 / 4228 / 1 | 5/5 |
| tsan | on | 7/7 | 123 / 4228 / 1 | 5/5 |

Normal/ASan harness counts (the same in all four lanes):

| Target | Cases | Checks |
| --- | --- | --- |
| response_sources | 30 | 304 |
| forms_multipart_files | 5 | 47 |
| multipart_decoder | 9 | 63 |
| response_definition | 32 | 311 |
| response_writer | 16 | 162 |
| forms_multipart_route | 14 | 178 |
| task_cancellation | 10 | 1342 |
| forms_multipart_corpus | 1 | 1 |
| task_resume_signal_lifetime | 3 | 1812 |
| body_reader | 18 | 131 |
| forms_multipart | 6 | 52 |

TSan retains all 304 response-source and 178 multipart-route checks. The unchanged allocation-hook case `published_waiter_can_die_during_timeout_registration` skips only when the sanitizer owns global new/delete; two allocator-independent owner race cases stay active (1800 checks). Normal/ASan execute all three owner cases (1812 checks). Automake correctly reports the mixed suite as PASS. The existing skip-exit-code gate passes.

## Installed package proof

For every lane, `verify_install.py` executes:

```sh
make -C "$lane/src" install prefix="$lane/stage"
clang++-18 $flags -I"$lane/stage/include" "$PWD/test/headers/consumer_v3_multipart.cpp" -o "$lane/installed-consumer-multipart"
clang++-18 $flags -I"$lane/stage/include" "$PWD/test/headers/consumer_v3_multipart_runtime.cpp" -L"$lane/stage/lib" -Wl,-rpath,"$lane/stage/lib" -lhttpserver -o "$lane/installed-consumer-multipart_runtime"
LD_LIBRARY_PATH="$lane/stage/lib" "$lane/installed-consumer-multipart_runtime"
```

The three response vocabulary consumers use the same installed-include compile/run pattern. All **30 consumer executions** pass, including both runtime snapshot scenarios in each lane. There is no `HTTPSERVER_COMPILATION`, build/TLS configuration macro, source/private include path or uninstalled v3core linkage. `ldd` resolves `libhttpserver.so.3` to each lane's staged library. `nm -D -C` in baseline and all installations preserves `httpserver::forms::read_multipart(httpserver::exchange&, httpserver::forms::multipart_limits const&, httpserver::forms::part_sink&)`. The private helper adds no exported public symbol.

All staged shared-library identities are `stage/lib/libhttpserver.so.3.0.0`, with SHA256:

| Lane | Shared library SHA256 |
| --- | --- |
| normal-off | `32a49477cb007240845db3dad5c4d62d2d5aaec2820de5e77bc1b2d3ac190dc2` |
| normal-on | `20f2c9dbf06c60803418904cfdb72ff208afaf4d9706300df848744c37284374` |
| asan-ubsan-off | `d4d19d4b9302547782af2baa84dc8f15d386325a652dc07e16d8fc755bc11a58` |
| asan-ubsan-on | `f5432da8826c14c796b55679b660309e2c9cc7e5e27fdef152bc2d3bc52d8106` |
| tsan-off | `29599a2882246d3c1b7a754e4abd6295acdedf3f8fd05c9264ac34f85b003c2c` |
| tsan-on | `fb8114854b367d7364c69e0a79900c4f1848a7be06a0d9b1ce5265d44ce534a4` |

These public headers match source bytes in all six stages:

- `forms/multipart.hpp`: `1c700ba3918a5c5dc5c47b9648e3586e63bb3f36ed17c2c1e6cbfbb28e4c62da`
- `response_sources.hpp`: `d6e74debfb5fec18a6d50d3ceca6149df96dff7d116ce368ee600a1653d0fec7`
- `response_definition.hpp`: `b3292187e31ae6280341336791c64b3d5b426d9e0660602262302c8ada667fd6`
- `response_writer.hpp`: `47aa41357b68a5220be338b03b540cb0234bfa8d715b7f1cdc88d25057196b34`

## Unresolved extra finding and ownership

An **extra**, unchanged `task_race` target included in the initial TSan-off run failed with a timer-thread/manual_executor destruction race (`pthread_cond_destroy` versus `manual_executor::post`), exit **66**. Initial TSan-on extra selection passed 8/8; TSan-off passed the seven affected targets and failed this eighth extra target. The failure is preserved in `tsan-off/extra-task-race-failure.log` and `tsan-off/focused-check.log`.

One bounded equivalent base comparison compiled that target directly from unchanged main-v3 sources/headers and ran it once: **exit 0, four cases, 2002 checks** (`base-task-race{,-build}.log`). `base-task-race-identities.json` proves identical target/concurrency-header/suppression inputs. This does **not** reproduce or close the intermittent finding. No concurrency source, test or suppression changed. The caller explicitly owns a separate repair before TASK-185's final local gate; the final required affected seven-target TSan sets pass in both TLS modes. No failing required affected check is being deferred or replaced.

## Tracking, checks and reusable notes

All four TASK-187 implementation action items are checked. Card/index remain In Progress pending caller-owned Groundwork validation/finalization. `cpplint` passes all six changed C++ files, `git diff --check` passes, and `make -C build/task187-implementation/normal-off lint-littletest-skip-exit-code` passes. The new runtime consumer is registered in `check_PROGRAMS` with `SOURCES` for distribution and public-library linkage.

No configured task-executor memory store exists. Reusable notes: setup is `./bootstrap`, fresh out-of-tree configure, `make -C <lane>/src` before scoped test make. Set `TESTS`, `check_PROGRAMS` and `noinst_PROGRAMS=` together. Regenerate Automake/config.status after adding target rules to configured lanes. Unit tests use `test/littletest.hpp` in `test/unit`; consumer checks live in `test/headers`. Avoid temporary captured coroutine closures; a synchronous function can snapshot values into a private by-value coroutine without altering public ABI. A response lease must pin the exact span storage. Run ASan/TSan outside ptrace without disabling diagnostics.

Requirement traceability: PRD-V3N-REQ-021/024/025 and DR-V3-003/006 map to bounded multipart reads, delayed start, typed cancellation and begun-part cleanup. PRD-V3N-REQ-026/028/029 and DR-V3-005 map to explicit leased storage, one completion, immutable replay and bounded partial sends. PRD-V3N-REQ-038 maps to the retained multipart decoder/route/files/corpus behavior and migration ownership notes.

BSD/macOS/Windows and other nonlocal checks were not executed; they remain CI/v3-PR owned under AGENTS.md. The unresolved extra intermittent race remains caller-owned before TASK-185's final gate. No merge, cleanup, push, tag, release publication or deployment was performed by this implementation agent.
