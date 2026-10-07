### TASK-143: Implement HTTP/2 staged GOAWAY and deadline drain

**Milestone:** M10 - TLS and HTTP/2
**Component:** HTTP/2 connection engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/2 staged GOAWAY and deadline drain for libhttpserver v3.0.

**Action Items:**
- [x] Send staged GOAWAY and preserve accepted-stream work.
- [x] Refuse later streams with correct identifiers.
- [x] Report deadline completion or cancellation through drain ticket.

**Dependencies:**
- Blocked by: TASK-110, TASK-142
- Blocks: TASK-144, TASK-145, TASK-170

**Acceptance Criteria:**
- Accepted streams finish; later streams are refused with correct GOAWAY identifiers and deadline result.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-032
**Related Decisions:** DR-V3-008

**Status:** Complete

**Prepared implementation (TASK-143):**

- Added fixed, separately budgeted initial GOAWAY, transmitted PING barrier,
  and final accepted-stream cutoff. Terminal GOAWAY cannot increase that cutoff.
- Admission closes immediately; accepted DATA, trailers and response output
  continue. Refused headers still update HPACK and discarded DATA refunds
  connection credit through the existing bounded paths.
- The internal HTTP/2 seam returns the existing drain ticket. Its owner must
  pump at the absolute deadline; ticket waiters only request cancellation.
  Completion requires stream retirement and all response/control bytes retired.
- Handler resumes use an owned executor adapter to defer cancellation reaping
  until the active resume returns. Frame witnesses retain the adapter lease for
  in-flight signals after teardown; a deterministic ASan regression reproduced
  the dangling-affinity failure before this lifetime fix.

**Local implementation verification:**

- Autotools C++20 build: `./bootstrap`, configuration in `build-v3` with
  `CPPFLAGS=-I/opt/homebrew/include`, `LDFLAGS=-L/opt/homebrew/lib` and
  `--disable-examples --disable-v3-tls`, then `make -j2`: passed.
- Built and ran all 13 planned suites plus `task_core`, `task_cancellation`,
  `task_resume_signal`, `task_resume_signal_lifetime`, and `task_race` with
  Automake `check -j1` selecting those same `check_PROGRAMS` and `TESTS`:
  **18/18 passed**. `http2_drain` has **21 tests, 3392 passing checks**.
  The native-server suite needed localhost socket permission: sandbox binding
  returned EPERM; the identical suite passed with that restriction removed.
- Separate `build-asan` configuration used `--disable-shared`,
  `CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'`, and
  matching sanitizer linker flags. Rebuilt the native archive and ran
  `http2_drain`, `http2_reset`, and the five concurrency suites above with
  `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`: **7/7 passed**.
- `make -C build-v3 check-local`, changed-file cpplint, changed-source CCN <= 10,
  the repository file-size gate and `git diff --check`: passed.
- The full-tree complexity scan reports existing, unchanged violations in
  `dispatch_request` (request_lifecycle.cpp), `valid_peer_pattern`
  (server/options.hpp), and `accept_one` (io_poll_sys.hpp). This task introduces
  no changed-source complexity violation.
- BSD, Windows and other nonlocal platform checks remain CI/v3-PR-owned and
  were not executed locally. Independent validation, final task closure,
  runner commit, merge and cleanup remain with the caller.
