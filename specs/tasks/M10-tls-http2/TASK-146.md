### TASK-146: Implement Windows IOCP managed I/O backend

**Milestone:** M10 - TLS and HTTP/2
**Component:** Private operation I/O
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide Windows IOCP managed I/O backend for libhttpserver v3.0.

**Action Items:**
- [x] Post AcceptEx, WSARecv and WSASend with owned OVERLAPPED storage.
- [x] Keep operations alive through completion or cancellation packets.
- [ ] Run poll-oracle and out-of-order completion traces on Windows.

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-147, TASK-148, TASK-182

**Acceptance Criteria:**
- OVERLAPPED storage survives completion/cancel races; oracle scenarios pass on native Windows.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014
**Related Decisions:** DR-V3-004

**Status:** In Progress

**Implementation evidence (2026-10-07):**
- Added a private managed Windows IOCP backend with provider-compatible AcceptEx,
  staged WSARecv/WSASend buffers, targeted CancelIoEx, token validation, and a
  separate outstanding registry drained before driver teardown. External Windows
  mode continues to use the readiness backend.
- Added production staging tests and native IOCP S1-S18 contract instantiations,
  packet gates for cancellation/re-adoption/out-of-order delivery, accepted-socket
  cleanup, reentrant/concurrent close, and a managed native HTTP loopback journey.
- Local C++20 library/focused builds passed. Focused Automake run: 9 passed,
  2 intentional platform skips (IOCP and epoll), 0 failures. Initial loopback
  failures under sandbox restrictions passed when rerun with listener access.
- Production staging passed ASan/UBSan (4 tests, 14 checks). Changed C++ files
  passed cpplint. `make -C build check-local` passed, including public headers,
  examples, repository lints, docs, install layout, and native linkage audit.
- Windows-target C++20 syntax compilation of the new backend, factory, staging
  test, and IOCP suite passed with `-Wall -Werror` using LLVM-MinGW Windows headers.
  An optional TLS-off cross-link produced an x86_64 Windows IOCP test executable;
  its probe supplied `_POSIX_THREAD_SAFE_FUNCTIONS` and `ntstatus.h` for existing
  Date/entropy portability dependencies. This is compile/link evidence only.
- Native Windows execution, including WSAPoll oracle agreement and real packet
  traces, remains unexecuted locally. MINGW64 CI explicitly checks the scenario
  log; per AGENTS.md this nonlocal evidence is assigned to CI and the v3 PR and
  does not block local implementation. The Windows action item stays unchecked
  until that execution is observed.
- Task status remains In Progress for the caller's validation/finalization phase.
