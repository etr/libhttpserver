# FEATURE-ci-v3-runtime-green

Status: Stage 1 implemented with focused proof; complete CI and remaining supported runtime repairs are pending. Groundwork validation is explicitly prohibited until FULL CI IS GREEN.
Base: `v3` at `8f3083977f49706e66cefbb3ee8b6f6685469719`.
Working branch: `task/FEATURE-ci-v3-runtime-green` (retain this one worktree through CI).
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
