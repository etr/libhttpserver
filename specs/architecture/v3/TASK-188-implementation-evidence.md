# TASK-188 implementation evidence

Implementation worktree: `/home/etr/progs/libhttpserver/.worktrees/TASK-188`. Branch `task/TASK-188`; base `v3` at `4de49a36462600c62010d517f2ba86f236b6dfc6`. Manual Groundwork implementation owns its commit; caller owns validation, finalization, merge and cleanup. Task card/index remain In Progress pending that handoff.

## Established cause and TDD

Frozen TASK-185 normal TLS-off/on `peer_policy.log` stop at `concurrent_mutation_and_reads` and report exit 124 under the original 180-second deadline. Their source receipt identifies the unchanged peer-policy implementation as `ec279b7f87468f5efe7f47b31f6fc0931254475911629e1aee1108f4ad558236`. These receipts were read without modification. Their timeout alone did not prove the cause.

The invocation journal is `/home/etr/progs/libhttpserver/.worktrees/TASK-188/.debug/peer-policy-mutation-starvation-mpOFOu/journal.md`. A direct unchanged original test baseline happened to pass: 9 cases / 64 checks, 69.9 ms. The stall is schedule-dependent. The bounded discriminator retains the original two writers, 200 mutations each, and six continuously running readers, establishes reader pressure with a start gate and 600 completed iterations, and adds 1024 disjoint unmatched deny patterns to extend overlapping scans. Its independent `steady_clock` bound is 15000 ms; no sleeps, reader/mutation reductions or release deadline extensions were introduced.

Verified RED against the actual base source (and separately its original shared library) showed 0/400 completed mutations while 6,969,291 and 9,061,804 reader iterations completed. After the test first records failure, reader-pressure cleanup releases the pending writer acquisitions; both return immediately (2 completed mutations before their safe stop boundaries), all workers join, and littletest reports two assertion failures with exit 2. This disproves a lock cycle and supports reader-preferred shared-mutex starvation across scans/getters. The final stop-token-aware regression was also compiled against the copied base source and failed with 0/400 mutations, 3,778,384 reader iterations, and 2 cleanup completions. Its steady-clock observation was 15008 ms including joins. External timeout 50s is only containment; it did not produce the failure.

Direct RED/GREEN commands use `/usr/bin/clang++-18 -std=c++20 -pthread -O1 -g -Isrc -Itest test/unit/peer_policy_test.cpp <base-or-current-peer-policy.cpp> src/detail/net_address.cpp -o <binary>`. Base source is copied by `git show v3:src/detail/peer_policy.cpp`; no production worktree revert was needed. The complete unchanged original test source is preserved as `.debug/peer-policy-mutation-starvation-mpOFOu/original-peer-test.cpp` (SHA256 `d249c153f8d51967c22d11251c57a431b1e930584064e19adf2b8326f867e84b`). Initial GREEN executes 11 cases / 2699 checks without failures; 400/400 mutations complete in 13 ms including joins. A focused direct TSan probe completes 400/400 in 160 ms without diagnostics. Final six-lane results below supersede those exploratory timings.

## Publication repair and cleared doubts

One immutable generation owns deny/allow vectors, mode, enabled and derived armed state. A writer mutex serializes copying the latest generation and publishing the candidate, preventing completed additions/removals or stance updates from being overwritten by independent writers. Parsing remains before writer serialization. C++20 atomic shared-pointer release publication and acquire loads retain vector lifetime through both scans. Atomic shared-pointer operations are not claimed to be lock-free. A plain exclusive mutex was rejected because it retains global scan contention and does not implement architecture §3's publication responsibility.

The scalar `armed()` fast path remains one acquire load. Writers publish the generation before release-storing its derived gate. After passing that gate, classification checks the retained generation's own armed state (including enabled). This closes the old gate/disabled-state mixing opportunity. Calls overlapping publication may use an older complete generation; subsequently synchronized operations observe the completed mutation. No cross-call armed/enabled or mode/classify equality is asserted while mutations run.

Concurrency doubts are covered by the bounded reader-pressure regression; two independent 200-operation add/remove writers overlapping a third 400-iteration mode/enabled writer, with all settled deny/allow entries checked; six-reader legal-generation classification during 400 disable/mode/re-enable cycles; synchronized disabled/re-enabled checks; and full sanitizer transport/lifecycle coverage. Candidate allocation fails before publication, leaving the previous generation intact. Add/remove retain throwing allocation behavior and typed invalid-pattern outcomes. Existing `noexcept` setters now allocate to copy a generation: allocation failure terminates, with no silent update dropping or public declaration changes. This tradeoff is explicitly recorded in architecture §3. No custom reclamation queue or additional production dependency is introduced.

Unit workers use stop-token-aware C++20 jthreads, including exception cleanup of partially constructed groups. Successful-path readers stop only after writer completion; failure is recorded before pressure is released. Parser identities, overlap/no-op behavior, both truth tables and reasons, family/unspec rules and public header types retain their existing tests. Listener/request lifecycle code needed no production changes.

## Local lane configuration

All six fresh lanes use Ubuntu Clang 18.1.3 (`/usr/bin/clang-18`, `/usr/bin/clang++-18`), C++20, `--disable-examples`, and common flags `-std=c++20 -pthread -O1 -g -fno-omit-frame-pointer`. TLS-off uses `--disable-v3-tls`; TLS-on uses `--enable-v3-tls`, `V3_TLS_CFLAGS=-I/home/etr/progs/libhttpserver/build/tooling/provider/include`, `V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'`. The existing provider is reused, not rebuilt.

ASan/UBSan adds `-fsanitize=address,undefined -fno-sanitize-recover=undefined -fsanitize-address-use-after-scope`; TSan adds `-fsanitize=thread` and the established `-DLHS_SANITIZER_OWNS_OPERATOR_NEW` allocator-ownership define. Executions run outside ptrace with:

```sh
ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
TSAN_OPTIONS=halt_on_error=1:suppressions=/home/etr/progs/libhttpserver/.worktrees/TASK-188/test/tsan.supp
```

The suppression file is unchanged: SHA256 `bef8512659b644216f9b4ad3640fd77cf911a11c3e9899f8f5ff36b32f7aee85`. No diagnostic-disable options or new suppressions were used. TASK-200's separately owned timer/manual-executor race and its task_race target were not edited or added to the focused selection.

Task-local runner `build/task188/run_lane.py` records exact configure, library, scoped test, installation, compiler and runtime commands, exit codes and durations in each lane's `receipt.json`. Libraries build first with `make -j2 -C src`; the affected selection is scoped through TESTS, check_PROGRAMS and noinst_PROGRAMS together:

```sh
tests='peer_policy net_address ip_controls_corpus request_lifecycle server_hooks hooks_corpus server_options_validate native_server native_http1_e2e external_http1_e2e consumer_v3_peer_policy consumer_v3_net_address consumer_v3_hooks v3_header_hygiene consumer_v3_peer_policy_runtime'
make -j2 -C test check TESTS="$tests" check_PROGRAMS="$tests" noinst_PROGRAMS=
timeout 180s test/peer_policy
```

Normal TLS-off/on also compile and execute the complete original unchanged 9-case source against the installed package under `timeout 180s`.

## Installed public listener evidence

The new distributed `test/integ/consumer_v3_peer_policy_runtime.cpp` compiles from a copied standalone directory against each staged `<lane>/prefix/include` and installed shared `libhttpserver`, with no HTTPSERVER_COMPILATION or private/source include path. The public readiness example helper is copied alongside it. `installed-compile.log`, `installed-ldd.log` and `installed-run.log` identify the exact compile, selected library and traffic. Each receipt hashes staged include/library and executable identities. TLS refers to library configuration; these listeners deliberately use plaintext HTTP/1.

Each loop mode uses one ephemeral loopback server throughout the live updates. Independent OS socket clients verify HTTP 200 / body `policy-ok`, handler-driven deny, disable/re-enable, remove, reject-all, allow and accept-all changes, restoration without restart, deny-over-allow under reject-all and allow-over-deny under accept-all. A separate 127.0.0.2 administrative client remains allowed while 127.0.0.1 is refused. A response following the completed mutation synchronizes every subsequent probe. Already accepted keep-alive revalidation and new-connection refusals both observe zero application bytes. Each scenario observes 7 accepted and 5 refused accept hooks plus typed `peer_refused` request completion for the already accepted connection. Both managed and externally driven public readiness modes pass; success/failure cleanup closes clients, initiates stop, wakes/joins the external host and stops the server while handlers/counters remain alive.

The first exploratory consumer smoke exposed a fixture error: temporary owning hook handles immediately deregistered the observations. Retaining both handles through stop fixed the fixture; all transport assertions already passed. This was not a listener fault and required no hook/publication refactor.

## Final gate results

All six final lane receipts pass. Each builds the library, executes all 15 affected programs, runs the complete strengthened peer_policy binary under the unchanged 180-second containment, installs the package, and compiles/runs the standalone public consumer in both loop modes. All final commands exit 0; no sanitizer diagnostic appears in affected test, direct peer or installed consumer logs.

| Lane | Programs passing | Harness cases / checks / skips | Writer completions | Steady-clock mutation bound observation | Installed loops |
|---|---:|---|---|---|---|
| normal-off | 15 | 179 / 4099 / 0 | 400/400 | 17 ms / 15000 ms | managed + external pass |
| normal-on | 15 | 179 / 4099 / 0 | 400/400 | 10 ms / 15000 ms | managed + external pass |
| asan-off | 15 | 179 / 4099 / 0 | 400/400 | 60 ms / 15000 ms | managed + external pass |
| asan-on | 15 | 179 / 4099 / 0 | 400/400 | 63 ms / 15000 ms | managed + external pass |
| tsan-off | 15 | 179 / 4097 / 1 | 400/400 | 215 ms / 15000 ms | managed + external pass |
| tsan-on | 15 | 179 / 4097 / 1 | 400/400 | 210 ms / 15000 ms | managed + external pass |

Every peer_policy execution runs all 11 cases / 2699 checks / 0 skips. Normal and ASan/UBSan execute 179 cases / 4099 checks / 0 skips across the affected selection. TSan executes 179 cases / 4097 checks, with one explicitly skipped allocation-count section described below; all policy, listener, hook, lifecycle and cancellation behavior remains active.

| Program | Cases | Normal/ASan checks | TSan checks / skips |
|---|---:|---:|---|
| peer_policy | 11 | 2699 | 2699 / 0 |
| net_address | 13 | 92 | 92 / 0 |
| ip_controls_corpus | 2 | 19 | 19 / 0 |
| request_lifecycle | 31 | 173 | 171 / 1 |
| server_hooks | 13 | 61 | 61 / 0 |
| hooks_corpus | 4 | 4 | 4 / 0 |
| server_options_validate | 19 | 181 | 181 / 0 |
| native_server | 20 | 108 | 108 / 0 |
| native_http1_e2e | 60 | 708 | 708 / 0 |
| external_http1_e2e | 4 | 52 | 52 / 0 |
| v3_header_hygiene | 2 | 2 | 2 / 0 |

The three existing compile-only public header consumers and the new public runtime consumer also pass in every lane. Each private and installed runtime invocation reports, for each loop mode, 7 accepted hooks, 5 refused hooks and 1 typed peer_refused completion. Accepted HTTP status and body are asserted on every accepted request; refusals assert zero application bytes. Installed ldd outputs select each lane's own prefix/lib/libhttpserver.so.3.

Both normal lanes additionally run the complete original unchanged 9-case / 64-check peer_policy source against the staged installed shared library under timeout 180s: exit 0, no failures/skips. Original concurrent-case times are 3.167 ms (off) and 1.105 ms (on); exact process wall times and command identities are in receipts. These retain the original two-writer, 200-mutations-each and six-reader workload.

A demonstrated fixture blocker required an explicitly authorized scope addition. Both initial TSan request_lifecycle links failed because the fixture's unconditional global new/delete override conflicts with TSan-owned definitions. Frozen initial-allocator-link-failure-focused-check.log and initial-allocator-link-failure-receipt.json preserve this RED in each TSan lane. The existing repository sanitizer-ownership guard pattern now avoids that override only when the sanitizer owns the allocator. In zero_hook_commit_allocates_nothing, both functional commit-call assertions still run; only its two allocation-count assertions skip explicitly. Thus TSan request_lifecycle executes 31 cases / 171 checks / 1 skip, while normal/ASan retain all 31 cases / 173 checks / 0 skips. Normal TLS-off/on were rechecked after this final fixture edit; lifecycle-final-recheck.log and receipt entries prove that behavior. No production lifecycle/timer repair, new suppression or diagnostic disabling was introduced.

cpplint passes all five changed C++ files. The littletest skip/result contract passes (77 for all-skip, 0 for mixed success+skip, failure count for fail+skip). git diff --check passes. The new consumer source is registered in Automake check_PROGRAMS/SOURCES and its reused public host helper is already distributed by examples/Makefile.am. All implementation action items are checked; Groundwork validation and task completion/merge remain caller-owned.

Final source identities:

| File | SHA256 |
|---|---|
| src/detail/peer_policy.cpp | `c33fb51f736223d33bd81a9338233e77aca79caf41ff7972a6a809f455c0fc31` |
| src/httpserver/server/peer_policy.hpp | `173444217d10dd075009445f8579f8c12b0b7baa53580df444179f334e0cb785` |
| test/unit/peer_policy_test.cpp | `cb89e479d7484399fea02146932d8f8129dbb11ecd6edcb7b9cdb151b376eaeb` |
| test/unit/request_lifecycle_test.cpp | `574fb86e1ba771ae5c3811c4add291293d46f71a10119ebfa4cf3159010bd82c` |
| test/integ/consumer_v3_peer_policy_runtime.cpp | `8a45377c4dbc8c3fe4fcfd37b0b81773e9f42bd47b1ec4ed8ec0674952ad33dc` |

final-source-identities.json records all changed build/test source hashes, unchanged suppression identity, historical log hashes and tested object timestamps. All six peer-policy, lifecycle and consumer objects are newer than their final source files, and staged consumer/header copies exactly match final repository bytes. Each lane receipt identifies the installed shared library, installed policy header and executable hashes; final-summary.json includes every target's case/check counts and all retained log hashes. No local gate remains incomplete.

## Artifact and ownership handoff

Evidence root: `/home/etr/progs/libhttpserver/.worktrees/TASK-188/build/task188`; journal root: `/home/etr/progs/libhttpserver/.worktrees/TASK-188/.debug/peer-policy-mutation-starvation-mpOFOu`. Caller must archive these ignored artifacts before cleaning the task worktree. Logs retain baseline, RED/GREEN, initial fixture diagnostics and final lane executions separately. `final-summary.json` contains per-target case/check counts and log hashes. Final source/runtime identities are retained in lane receipts and the journal identity manifest.

BSD, macOS, Windows and other nonlocal backend runs are unexecuted and CI/v3-PR owned under AGENTS.md. Local sanitizer evidence remains required. TASK-185 frozen logs and incomplete release status, unrelated FEATURE work and TASK-200 ownership are preserved. No validation-loop, merge, cleanup, push, tag or release publication is performed by this implementation executor.

No executor memory store is configured or callable. Reusable setup notes: `./bootstrap`, fresh out-of-tree configure, `make -C <lane>/src` before scoped test make; regenerate Automake after target registration; unit tests use test/littletest.hpp and test/unit; standalone public clients use staged include/library paths and the public v3_host_poll.hpp example helper. Hook registration returns an owning RAII handle which must survive observation. Sanitizer execution must remain outside ptrace with diagnostics enabled. Agent memory notes are retained here for the next task.
