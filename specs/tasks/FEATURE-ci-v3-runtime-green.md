# FEATURE-ci-v3-runtime-green

Status: Stages 1, 3, 4, 5 and 6 implemented with focused proof; complete CI and remaining supported runtime repairs are pending. Groundwork validation is explicitly prohibited until FULL CI IS GREEN.
Base: `v3` at `8f3083977f49706e66cefbb3ee8b6f6685469719`.
Working branch: `task/FEATURE-ci-v3-runtime-green`. User requested integration into `v3` before full CI completion; retain incomplete verification explicitly.
Plan: primary checkout `.groundwork-plans/FEATURE-ci-v3-runtime-green-plan.md`.
Journal: primary checkout `.debug/v3-ci-green-VSvXxx/journal.md`.
Stage evidence: `.debug/v3-ci-green-VSvXxx/runtime-green-stage1/`.

## Stage 1: default Windows timer shutdown

- [x] Compare cached shutdown detection with event substitution and unconditional singleton leakage.
- [x] Preserve actual native62 RED and reliable LLDB unwind; do not relaunch original captured suites.
- [x] Add and observe a failing bounded Windows DLL exit regression, with ordinary explicit timer retirement coverage.
- [x] Cache the documented Windows shutdown query before constructing the default timer thread; abandon that service only during actual process termination. Preserve ordinary cleanup, POSIX behavior and public headers/API/ABI.
- [x] Register `task_timer_shutdown` in Automake; verify DLL exit, explicit retirement, ordinary static-executable teardown, existing Linux resume/lifetime tests, changed-file lint and diff.
- [ ] Full exact-source CI on the working branch, including actual MSYS2 package/archive lanes.
- [ ] Groundwork validation, only after full CI is green; caller owns publication and eventual merge/cleanup.

The actual original CI/local native62 program completes all 734 checks but hangs during DLL process detach. The accepted 24-frame LLDB unwind reaches `timer_queue` condition-variable destruction from `ExitProcess`/`LdrShutdownProcess`. Evidence is retained at `.debug/ci-windows-dump-entry-dbl8orzx/offline-analysis/mac-approved-result.json` and `mac-lldb-session.stdout.txt`; the earlier DbgHelp `.bss` chain is rejected. The original 180-second gate remains FAILED/NOT_RECHECKED.

Microsoft documents that process exit kills the other threads before DLL detach, so even lock acquisition, joining and synchronization-object destruction can deadlock. The private Windows owner resolves `RtlDllShutdownInProgress` before creating its queue/thread, and releases its ownership only when that cached function reports process termination. There is no module lookup, allocation, locking, joining or synchronization destruction on that path. The operating system reclaims this single process-lifetime service. Ordinary destruction still deletes the queue; the POSIX factory is byte-identical. If the documented Windows 10+ query is unavailable, the ordinary cleanup fallback remains; legacy-platform shutdown is not claimed fixed.

Requirements preserved: PRD-V3N-REQ-024/025 and architecture v3 section 3.1 (timeout/cancellation, exactly-once delivery and executor affinity). Public representation, scheduling, cancellation, deadlines, explicit timer ownership and callback delivery are unchanged. No forced application exit, DLL pinning or public shutdown interface is introduced.

## Focused proof and context limits

A C-only consumer of the retained actual CI DLL reaches its default timer factory and prints `DEFAULT_TIMER_READY`, then fails to exit within 9 seconds; its retained handle is retired, with total 9.449 seconds. No C++ standard-library object crosses that ABI boundary. This is a new minimal regression, not another original native62 launch.

The new registered C++ regression runs a matching small three-owner DLL (`resume_signal.cpp`, `task.cpp`, `executor.cpp`) with explicit retirement and default-exit children. The final test against original timer source is RED: exit 1, child wait timeout 258 and destruction-during-shutdown marker, total 9.372 seconds. Against fixed source it is GREEN: exit 0, total 0.491 seconds, no shutdown destruction marker. Each child has a 9-second wait and at most 1 second for owned retirement. An inherited event observes pending-capture destruction during actual process termination, so the assertion also protects runtimes whose condition variable does not itself hang. Static-executable normal CRT destruction is permitted by this test and passes separately. Ordinary explicit retirement joins the worker and releases a capture with an hour-long pending deadline.

Linux focused tests pass: `task_timer_shutdown`; `task_resume_signal` 11 tests/134 checks; `task_resume_signal_lifetime` 3 tests/1816 checks. These use current source compiled with GNU13/C++20/pthreads and only the three owning concurrency translation units. Changed-file cpplint and `git diff --check` pass. `autoreconf -fi` registers the new program and inherited library linkage; existing AC_OUTPUT and .PHONY warnings are recorded without unrelated repair. No full baseline or broad suite was run.

Native Windows context is Windows 11 build26200, cross LLVM-MinGW23.1.4/MinGW15 with its UCRT startup/headers, MSYS2 GCC15.2 standard-library headers/import libraries in an owned cache, and the retained actual CI MSVCRT libstdc++/libgcc/winpthread DLLs. Actual CI uses Windows Server2022 and LLVM23.1.0 with MSYS2 mingw64 throughout. This focused mixed-CRT proof is qualified; it does not replace full actual CI. No provider, SDK or compiler was built or globally installed. A first downloaded UCRT GCC package was unused after original imports identified MSVCRT.

Preserved setup failures: sandboxed WSL native launch was unavailable before normal escalation; initial future-based fixture had unresolved libstdc++ TLS symbols, then a corrected TLS build exposed missing runtime once-call exports (native entry-point status C0000139). That setup failure is not RED. Replacing only test readiness synchronization with an already-supported Win32 event corrected the fixture boundary; the corrected native failure above is the behavioral RED. No production workaround was based on the setup error. Raw commands, logs, receipts, hashes and bounded ownership cleanup are retained in stage evidence.

Darwin PSK failures, Windows host WSAPoll consumer failure and incidental Linux/macOS/BSD assessments retain their previous UNKNOWN/closed-budget states. No unrelated source repair, original diagnostic retry, push, CI dispatch, review agent, Groundwork validation, merge or cleanup occurred in stage 1.

## Related WSAPoll error visibility

- [x] Immediately save `WSAGetLastError()` when the existing `WSAPoll` call fails and include its numeric value in the existing exception message.
- [x] Preserve interrupted-poll handling and all polling, timing and shutdown semantics.

This caller-authorized message change follows proven timer stage commit `70d0fba7`. It supplies useful error evidence in the next already-required CI run; it does not diagnose or repair the prior consumer exit 6, whose cause remains UNKNOWN and captures remain closed. There are no per-poll traces, socket queries, clocks, locks or error suppression changes.

A bounded Windows test substitutes only the external WSAPoll boundary to return WSAEINVAL through the actual Winsock last-error API. Before the change it returns 2 because the exception omits numeric 10022 (0.310s); after it exits 0 (0.303s), also checking that WSAEINTR still produces no exception. This controlled message regression is not a reproduction of the original consumer failure. Actual Windows compilation, GNU syntax checking of `examples/external_event_loop.cpp`, changed-header cpplint and diff checks pass. Commands, sources and receipts are under stage evidence `host-poll-message/`.

Additional ordinary cleanup proof: a static-linked Windows default service releases its pending callback during normal executable CRT teardown before a later registered atexit observer checks the capture; exit 0 in 0.391s (`ordinary-default-native-test.json`). The default service is therefore not unconditionally immortal.

## Stage 3: FreeBSD evidence transport setup

- [x] Preserve actual current8f native package PASS, redundant automatic-copyback FAIL, and explicit fresh SCP recovery/upload PASS as separate receipts.
- [x] Set `copyback: 'false'` on the pinned FreeBSD VM action in package validation and release acceptance.
- [x] Pass the existing 13 focused workflow tests, actionlint and exact source-preservation proof.
- [ ] Full working-branch CI with the corrected VM setup (not dispatched here).

Current8f package job `121358499066` passed installed native TLS=no at 03:49UTC, TLS=yes and identical installed headers at 04:03UTC. Its later automatic whole-workspace rsync copyback failed with protocol error 12 after receiving 4,049,536,821 bytes. Explicit fresh SCP recovery then succeeded and uploaded artifact `12626459963`, 776,035,888 bytes, producer-reported digest `597ded253263153a8ae4bb617df42defaaceb55c930978e013e4063253532894`. This is retained raw-log/metadata evidence; the artifact was not downloaded for stage3. It does not prove the separate cookie archive translation-unit gate, which remains pending.

The exact pinned action implementation SHA256 is `7f9e5e36ceae15937ec0aabbf7e4ce4ab2855b36aa3dfa0ce8e79e7c0fb9569e`; its post-run step 7 runs only when the lowercased copyback input is not `'false'`. Initial synchronization and guest execution are distinct earlier operations. The input is declared in the [pinned action metadata](https://raw.githubusercontent.com/vmactions/freebsd-vm/c9f815bc7aa0d34c9fdd0619b034a32d6ca7b57e/action.yml). Disabling only this redundant post-run copyback leaves the explicit required SCP transport responsible for retrieving evidence. The underlying rsync connection-loss mechanism is not claimed diagnosed.

Whole-file byte comparison proves that removing the single added scalar from each workflow restores the original file exactly. Guest prepare/run/trap/native commands, initial sync settings, pinned actions/toolchains, source checks, every gate, SCP error propagation, artifact paths and existing fallback handling are therefore unchanged. An existing fallback is preserved if present; automatic copyback is no longer a promised source for it. Failed SCP and missing evidence remain failures.

Focused checks: `python3 scripts/test-v3-ci-workflows.py FreeBSDNodePrerequisites -v` (9 tests), `python3 scripts/test-v3-package-workflow.py -v` (4 tests), cached actionlint on both workflows, and `git diff --check` all pass. No new mirror test, VM, timeout increase, retry, transport framework, baseline suite, CI dispatch, push, merge or Groundwork validation was performed. Stage3 commands, outputs, source hashes and preservation proof are retained at `.debug/v3-ci-green-VSvXxx/runtime-green-stage3/` in the primary checkout. PSK source remains untouched while its fresh native authorization is pending.

## User-directed integration before CI completion

The user requested “merge back to v3, then keep fixing the remaining CI issues.”
Integrate the focused repairs and ChangeLog update now. Full CI remains incomplete;
Groundwork validation has not run and stays deferred until full CI is green.
This integration does not mark the overall CI repair complete or reopen closed
diagnostic batches. Draft PR #382 carries the original working-branch runs.

## Stage 4: PSK test fixture scheduling (2026-10-27)

- [x] Read the completed, previously authorized diagnostic receipts; keep their budgets closed.
- [x] Preserve the native Darwin RED: 14 tests/313 checks, three failures; held TLS1.3 cancellation callback already returned, and certificate-default TLS1.3 handshake timed out.
- [x] Release the held cancellation lookup explicitly after assertions and close; bound the callback wait to five seconds for failure cleanup.
- [x] Drain ready fragmented transport work and yield while waiting for provider work, preserving the original fragments and every deadline/assertion.
- [x] Reproduce the real certificate fixture timeout under coarse scheduling, then pass the same regression and all 14 PSK tests/313 checks.
- [x] Pass changed-file cpplint and diff checks.
- [ ] Native current-source CI, including Darwin selected archive acceptance.
- [ ] Resolve the remaining Windows external-loop failure from supported evidence.
- [ ] Close the FreeBSD selected-archive timeout gate with actual evidence.

Evidence and commands: primary checkout `.debug/v3-ci-native-repairs-PJFEkn/journal.md`.
Current c1 installed matrix passed Linux, Darwin, FreeBSD; Windows failed. Native/fuzz, CodeQL and Verify Build passed. Source-equivalent selected archive remains failed; no current-source green claim.

The diagnostic Windows four-test program failed one of 52 checks in 0.741 seconds with WSAPoll error10038 before stop was requested. Two bounded new socket-only probes on local Windows11 returned POLLNVAL successfully; they do not establish the Server2022 failure cause. No poll error suppression or guessed production fix was applied. Previous capture budgets remain closed.

Fixture root cause: the cancellation rescue unconditionally released after250ms, before the owner could assert cancellation under slower scheduling. The transport also slept after every41/73-byte fragment, charging scheduler delays to real handshake deadlines. A minimal real TLS1.3 certificate fixture with1ms sleeps delayed to15ms failed at2089ms/outcome8(timeout); draining progress first passed at625ms. An intermediate full coarse-scheduling check still failed two held/capacity assertions because idle sleeps could expire the400ms pre-lookup deadline. Yielding while idle preserved that deadline and passed all313 checks in1.029seconds, with zero skips. The100ms owner-responsiveness assertion, cancellation/timeout/capacity/late-result assertions, bounded drains and all protocol settings remain unchanged. Only test source changed; production TLS is unchanged.

Setup evidence is retained honestly: system OpenSSL was rejected before selecting the existing3.5.9 provider; the first suite launch used build-root cwd instead of test cwd and failed fixture path lookup. Correcting cwd, as required by the generated relative TLS_TEST_DIR, produced the meaningful behavioral check. Neither setup failure justifies a source workaround. No broad baseline or Groundwork validation was run.

## Stage 5: Windows allocator fault-injection fixture linkage

- [x] Identify the current PR383 failed program from one bounded selected artifact assessment (3,891,726 range bytes; no full ZIP or original execution).
- [x] Prove that executable operator-new replacement does not affect Windows DLL allocation, and that static linkage delivers the intended fault.
- [x] Keep all 22 ordinary readiness cases against the installed Windows DLL; require all 27 cases, including the five unchanged allocation-failure cases, against the installed static archive.
- [x] Make the required case names explicit in the inventory and reject every missing static fault case. Other platforms retain their original fault coverage.
- [x] Pass native Windows focused DLL22/438 and static27/486 gates, Linux28/497, 28 package coverage/matrix checks, changed-file lint and diff checks.
- [ ] Full exact-source CI with the corrected installed linkage profiles.

Current PR383 package job122630892885 passed native62 in4.812276s and external4 in0.572681s, then failed external_readiness_adapter27/486 with13 failures, all inside five allocation-failure cases. The assertion expecting bad_alloc was false and regular work completed. The PSK change cannot affect these allocation boundaries; this repair is within the accepted overall CI goal. Member CRC/SHA are verified; the full212MB ZIP digest is metadata-only. Assessment is closed.

A tiny native PE fixture reproduces the cause: executable allocation throws under its replacement operator new, DLL-owned allocation does not (exit1/0.362s). Linking the same owner object into the executable makes both throw (exit0/0.309s). Microsoft documents per-module DLL import binding: https://learn.microsoft.com/en-us/cpp/build/linking-an-executable-to-a-dll?view=msvc-170 . The chosen correction scopes allocation injection to linkage where it can actually reach the owner. Artificially throwing in the caller would not exercise the backend; a new production allocator hook or loader patch is unnecessary.

Only Windows shared compilation selects LHS_TEST_DLL_LINKAGE; this omits allocator replacement and the five fault registrations from that executable. All five original assertion bodies remain unchanged and mandatory in the installed static lane. The inventory names all27 ordinary/static cases, and the receipt validator requires the22 DLL cases or all27 static cases. No test skip or failed/empty receipt is accepted. Every original C++ assertion line is preserved. The other platforms keep all five fault cases in both linkage lanes.

Fresh current-source focused Windows backend DLL (16 owning translation units) passed22/438 in0.899s; matching static executable passed27/486 in1.237s, both zero skips and retired. Linux passes28/497 including its descriptor-zero case. Removing any one of the five fault names from the static receipt is rejected. Python coverage/matrix tests pass28 cases. Native setup initially missed net_address.cpp at link; adding that existing owning translation unit corrected the test build without a production workaround. Context is qualified local Windows11/cached LLVM23.1.4/GCC15.2 headers and retained runtime DLLs, not Server2022/MSYS2 CI. Evidence: primary `.debug/v3-ci-native-repairs-PJFEkn/allocator-linkage/`.

## Approved Windows capture closure

User approved exact430fa17e publication and ONE1/60 Windows original external-loop capture. Run40635371672 completed once:4tests/52checks/zero skips, exit0 in0.4701966s, retirement confirmed, stderr empty. Budget is CLOSED; no retry or remaining-time authority. This establishes nonreproduction in that context, not a fix or independence of the earlier WSAPoll10038 failure. The diagnostic branch remains NEVER MERGE. Only a proven repair may change production behavior.

Stage5 receipt-import closure: source review found the selected-archive importer independently required all cases in each linkage. A new real receipt-bundle regression was RED with `valid DLL/static split rejected: pruned platform suite`. Reusing the same coverage validator fixes this producer/consumer mismatch while still rejecting missing static fault cases. All14 platform-bundle tests and changed Python syntax/diff checks pass. This is part of the same proven setup repair; native binaries are unchanged, so their focused evidence is carried without repetition. Current-source CI remains pending.

## Stage 6: unique native Automake inventory

- [x] Preserve Darwin archive failure `duplicate/empty native suite inventory` after its native commands completed.
- [x] Reproduce the exact producer inventory command against a real configured Makefile: TLSyes271 entries/266 unique, five duplicated executables.
- [x] Remove only four redundant TESTS additions; keep each executable in check_PROGRAMS, which already feeds TESTS recursively.
- [x] Regenerate actual Automake/configured Makefiles and verify unique inventories in both TLS modes: yes266/266, no235/235. All five affected executables remain registered exactly once.
- [ ] Full current-source archive evidence with this registration correction.

The duplicated names are http_cookie, consumer_v3_cookie, consumer_v3_cookie_wire, protocol_limits and protocol_compression. TESTS already equals $(check_PROGRAMS); the extra TESTS += lines counted them twice. The strict receipt duplicate rejection is unchanged. No executable, assertion, script test, protocol setting or failure deadline was removed. This four-line configuration repair is proved by the actual producer command, not a source-text mirror test or a broad baseline rerun.

Evidence: primary `.debug/v3-ci-native-repairs-PJFEkn/native-inventory/`. Autoreconf succeeded with existing AC_OUTPUT/.PHONY warnings. Initial config.status invocation used the source directory instead of its configured build directory and failed before any native execution; rechecking/regenerating in the correct build directory fixed setup. No unrelated source or generated tracked file changed.

Current9c92 Windows package job122646921257 fails earlier at the public shared external consumer: exit6, captured stderr WSAPoll error10038. The selected820-byte process receipt was verified within a closed2,581,790-byte range assessment; no full artifact or original program ran. It contains no failed-call slot/state values. The earlier approved external4 capture remains CLOSED/PASS4/52/nonreproduction, and does not authorize running the distinct public consumer. No guessed polling or shutdown suppression was applied. A fresh consumer-specific bounded capture proposal is needed if further native diagnosis is required.

## Stage 7: Windows WSAPoll borrowed-closure tolerance and HTTP/1 drain-deadline watchdog (2026-10-30)

- [x] Correlate all retained Windows failures (external four-test suite twice, public shared consumer exit6) with the approved DIAG capture: the host loop dies on "WSAPoll failed (WSA error 10038)" while dispatch never failed and stopping was false.
- [x] Root-cause by contract analysis, not a new capture: `interest_snapshot` handles are borrowed and "retaining a snapshot does not extend any handle lifetime"; the library closes registrations under its registry mutex and never republishes dead records, so WSAENOTSOCK can only mean a snapshot handle closed between snapshot and WSAPoll entry (handle-value reuse makes it WSAENOTSOCK on busy runners and POLLNVAL on idle probes, reconciling the closed local probes).
- [x] Repair `examples/host_poll.hpp`: WSAENOTSOCK returns an empty batch (the contract's spurious-notification allowance); every reference caller re-acquires interests after each dispatch, and dead records are pruned from the next snapshot, so the loop provably progresses. All other WSAPoll failures still throw; POSIX POLLNVAL handling is unchanged. Document the host obligation in `docs/external-loop-contract.md`.
- [x] Root-cause the 2026-10-28 macOS/Darwin external4 drain stall (idle keep-alive connection never closed after `begin_drain`; host silent for the full 10s budget; remaining 1): `watchdog_terminal_locked()` treated the quiesce close mark as terminal, but HTTP/1 enforces the server drain deadline only through the watchdog (`enforce_timeout` is the sole HTTP/1 `scope_.expire_if_due` caller), so a stalled close handoff had no deadline enforcement left.
- [x] Repair: the close mark no longer retires the watchdog while a server drain deadline is pending; the deadline joins `plan_watchdog_locked` candidates through the existing `add_protocol_deadlines_locked` entry and fires through the unchanged global-expiry path. Deadline-less closes (request_close, response-Connection: close) stay terminal as before.
- [x] RED/GREEN regression `quiesce_with_drain_deadline_keeps_watchdog_armed` (unstarted engine, deterministic plan sample; deadline-less case pins unchanged terminal behavior) plus behavioral `quiesce_with_drain_deadline_still_closes_promptly`. RED verified against unfixed source, GREEN against the repair, five consecutive clean runs.
- [x] Linux focused gates: connection_engine 42/0 (x5), external_http1_e2e 4/0, websocket_drain 3/0, http1_websocket_upgrade 17/0, native_http1_e2e 62/0, external_readiness_adapter 28/0, examples/external_event_loop builds and serves; changed-file cpplint and `git diff --check` pass.
- [ ] Full exact-source CI on the working branch (mingw64/macOS/FreeBSD lanes remain nonlocal and deferred to CI).

## Stage 8: FreeBSD job timeout (2026-10-30)

- [x] Evidence: the 2026-10-26 acceptance FreeBSD job completed its native proof at 2h59m45s (57 seconds under the 180-minute job timeout); the 2026-10-28 package-validation FreeBSD job was cancelled by the 180-minute timeout at 3h3m mid-run. The workload (libc++ 20.1.8 runtime build + provider + both TLS lanes on a 2-CPU VM) legitimately straddles the limit.
- [x] Raise only the freebsd job `timeout-minutes` from 180 to 240 in both `v3-package-validation.yml` and `v3-release-acceptance.yml`; every other job keeps its existing timeout. `test-v3-package-workflow.py` (4/4) and `test-v3-ci-workflows.py` (31/31) pass.
- [ ] Full FreeBSD CI evidence with the corrected budget.

The 2026-10-26 FreeBSD tls-yes `check-v3-public-http2.py` shared-external failure ("no retired connection generation captured", one occurrence) is retained as an open flake: the consumer's `stale` check depends on the single-point `snapshot` having captured the soon-to-retire H2 connection's published registration, and the saved pair in that run was still live at `stale` time. No consumer change is made without a recurrence; the 2026-10-30 runs will decide.

Full local `make check -j1` (TLS=no, g++ 15.2, WSL2): 232/235 pass, 2 platform skips (iocp/kqueue), 1 FAIL `quic_flow_control` (12 checks expecting ceiling-rejection throws). Byte-identical failures with stage 7 reverted and at b71192c9, so pre-existing and unrelated to this repair; CI builds this test with clang where it passes. Recorded as a local g++ environment observation, not a regression.

## Stage 9: Windows audit client closure (2026-10-30)

- [x] The 2026-10-30 mingw64 package run (job c8dc2d64) passed every external/installed suite in both linkages for the first time (stage 7 repair holding) and advanced to `check-v3-public-tls.py`, which failed walking the Python TLS client: `AuditError: unresolved PE import: version.dll` from `_ssl.pyd` -> `libpython3.14.dll` (its import table lists VERSION.dll; api-ms-win-* names already match the existing pattern rule).
- [x] Verified against the real MSYS2 mingw64 packages (python 3.14.9-1, openssl 3.6.5-1, libgcc 16.2.0-6, winpthreads, libstdc++): after adding `version.dll` to `WINDOWS_SYSTEM`, the full `_ssl` client closure resolves -- remaining names are WINDOWS_SYSTEM entries or MSYS2 runtime DLLs that the same run's own recorded suite graphs already resolve through `V3_PACKAGE_DLL_DIRS`. The mingw-built integration closures (from the run's suites.json) import only allowlisted system DLLs.
- [x] One-line allowlist addition in `scripts/audit-v3-installed-package.py`; `version.dll` is a documented System32 component (version-information APIs). `test-v3-installed-package.py` 60/60; `test-v3-installed-consumer.py` 97/97 (one unrelated compile-timing flake on the first local run, clean on rerun).
- [ ] Full mingw64 CI evidence past the client audit.

## Stage 10: Windows public audit client linkage (2026-10-30)

- [x] Evidence: the 2026-10-30 mingw64 package job (124062430922) and the Windows acceptance job (124062636691) both advanced past every earlier gate (installed suites, traffic cells, compiled operations and the Python TLS client audit of stage 9) and failed at the same place: linking the independent TLS client in `check-v3-public-tls.py`, with `undefined reference to __imp_WSAStartup/__imp_socket/__imp_htons/__imp_htonl/__imp_connect/__imp_closesocket` (tls_client.cpp already carries its own `_WIN32` Winsock path; only the link line lacked the system library).
- [x] Repair in `scripts/check-v3-public-tls.py`: the independent client is named with the platform `.exe` suffix and linked with `-lws2_32` on `os.name == 'nt'` (POSIX targets unchanged), and the provider `openssl` tool is invoked with its explicit `.exe` name on Windows, matching `run-v3-release-ci.py`.
- [x] Same-class parity repair in `scripts/check-v3-public-http2.py` (preemptive, code-audit driven, not yet exercised on Windows): consumers and examples get the `.exe` suffix and the `ws2_32/mswsock/bcrypt` system libraries replace the POSIX-only `-pthread`/`-rpath` line, matching the `check-v3-installed-consumer.py`/`check-v3-public-tls.py` convention; runtime DLL resolution stays on the `runtime_environment` PATH/V3_PACKAGE_DLL_DIRS mechanism. Local gates: `test-v3-public-http2.py` 6/6, `py_compile` on both scripts.
- [ ] Full mingw64/Windows CI evidence past the public TLS client link.

## Stage 11: first FreeBSD native make-check exposure (2026-10-30)

- [x] Evidence (5.1GB failure artifact 12993618134, run 41055662147, analyzed in full): the FreeBSD acceptance lane passed the entire installed matrix (both TLS lanes incl. public-tls and public-http2) in ~18 minutes, then reached `gmake check` for the first time ever (every earlier acceptance run died earlier: the Oct-26 runs at the http2 stale flake/copyback, the Oct-28 runs at the 180-min timeout). The first `make check` ran the full 7200s command deadline and was killed with only `response_sources` still unfinished: test 24 `threaded_pipe_sends_single_winner` started and never printed a result. Four real test failures were also recorded: `response_definition` (checks 846/847, `file_send_truncated_mid_send_fails`, completed in 2.58ms with `done==0`), `native_protocol_memory` ("process memory measurement unavailable" — no FreeBSD branch), `consumer_v3_peer_policy_runtime` (line 86 = bind/connect of the 127.0.0.2 admin source: FreeBSD has no 127/8 loopback alias by default), and `integ/http3_full_gate_runner_test.py` (`make -j4` in the gate runner is BSD make on FreeBSD; needs gmake).
- [x] Repairs with proven causes: `native_protocol_memory_test.cpp` gains FreeBSD (kinfo_proc sysctl) and Windows (GetProcessMemoryInfo) resident-memory branches — the Windows acceptance make check needs the same branch; `run-v3-http3-full-gates.py` selects gmake when present (GNU-make Makefiles, BSD default make); the freebsd acceptance VM run script adds-or-verifies the `lo0` 127.0.0.2 alias (no-op where `ifconfig lo0` is absent, e.g. the Linux workflow tests).
- [x] Hang bound: `test/timeout-wrapper.sh` (EXTRA_DIST, `LOG_COMPILER`) wraps every native test with `timeout -k 60 600` where timeout(1) exists, so a recurrence cannot consume a whole CI make-check budget; hosts without timeout (macOS default) run exactly as before.
- [x] Budget: freebsd acceptance `timeout-minutes` 240 → 300 (observed: ~95 min prepare with the pinned-Node source build + ~18 min matrix + make check ×2 + evidence recovery).
- [x] Local gates: fresh worktree configure/build; `native_protocol_memory` passes under the new LOG_COMPILER wrapper; `test-v3-ci-workflows.py` 31/31, `test-v3-platform-ci.py` 20/20, `test-v3-http3-full-gates.py` 1/1; changed-file cpplint and YAML parse pass.
- [ ] Open flakes (no repair without a recurrence, per the standing http2-stale precedent): `response_sources` test 24 hang (one occurrence; the claim/close guard is a single atomic CAS and `pull_handle`'s fread cannot block on a pipe whose write end is closed — mechanism unexplained) and `response_definition` checks 846/847 (coroutine did not complete after the mid-send truncation). Both pass on Linux/macOS; the next FreeBSD run with bounded hangs decides.
- [ ] Full FreeBSD acceptance CI evidence.

## Stage 12: Windows TLS-on interop keylog portability (2026-10-30)

- [x] Evidence (mingw64 job 124232738670, Windows acceptance job 124233097485, both at b080f744): with stage 10 in place the mingw64 package lane printed `PASS installed consumer native TLS=no` — the entire installed tls-no matrix including public-tls and public-http2 passed on Windows for the first time. The tls-yes lane then failed building `libquic_interop_provider_a`: `src/detail/tls_session.cpp:127/129` use `O_NOFOLLOW`, `O_CLOEXEC` and `fchmod`, none of which exist on Windows.
- [x] Repair: `open_keylog()` gains a `_WIN32` branch using `_O_NOINHERIT` (the close-on-exec analog) and dropping `fchmod` (the creating open already owns the file); the POSIX branch is unchanged. This is the only site in `src/` using those identifiers (`quic_tls_session.cpp` is clean). POSIX-branch syntax check with the interop define passes locally; cpplint clean.
- [ ] Full Windows tls-yes CI evidence past the interop provider build (SNI/rotation scenarios and the tls-yes public audits remain first-exposure).

## Stage 13: Windows QUIC fixture polls (2026-10-30)

- [x] Evidence (mingw64 job at db7cefec, 41112829610): with the stage-12 keylog fix the tls-yes `make -j2` advanced past `libquic_interop_provider_a` and failed at the next noinst program: `test/integ/quic_interop_endpoint.cpp:3: 'poll.h' file not found`.
- [x] Repair: the interop endpoint's hand-rolled poll loop now goes through the shared `examples/host_poll.hpp` helper (WSAPoll on Windows, the same contract every external-loop consumer uses), preserving the connection-deadline folding and the one-second park bound; `chmod` of the log directory is POSIX-only. `test/integ/http3_udp_fixture.cpp` — the next `poll.h` includer in the same build set — takes a `_WIN32` branch on the same helper while its POSIX stdin-poll path stays byte-identical (the stdin command channel is driven only by the POSIX parity lanes).
- [x] Local gates: both files pass cpplint and `g++ -std=c++20 -fsyntax-only` with the full build flags; `integ/http3_full_gate_runner_test.py` 27/27 (it drives the gate runner that builds and runs these fixtures).
- [ ] Full Windows tls-yes CI evidence past the QUIC fixtures.
