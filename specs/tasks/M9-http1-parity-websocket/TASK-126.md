### TASK-126: Implement Linux epoll managed I/O backend

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide Linux epoll managed I/O backend for libhttpserver v3.0.

**Action Items:**
- [x] Map operation contract onto epoll edge/one-shot readiness.
- [x] Drain until EAGAIN and rearm correctly.
- [x] Compare accept/read/write/close traces with poll oracle.

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-182

**Acceptance Criteria:**
- Edge-triggered operations drain and rearm; differential scenarios match the poll oracle.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014
**Related Decisions:** DR-V3-004

**Status:** Complete


**Implementation verification (2026-10-06):**
- Linux managed servers select the private epoll driver; external mode retains the independent poll adapter. Nonblocking socket steps serialize with cancellation/release, and terminal deliveries occur outside the registry mutex. Monotonic incarnation tokens reject stale events; `close()` joins the driver before executor teardown.
- Linux/aarch64 in a disposable Colima container: strict C++20 Clang 19 with libstdc++12; 235 cases passed across epoll (33), shared fake/poll (41), native server, HTTP/1, connection engine, drain, external readiness/HTTP, and WebSocket drain suites. Real epoll regressions cover latent read/accept readiness, EAGAIN, combined direction rearm, backpressure, payload before EOF, selected-operation cancellation/release, fd reuse, and fatal wait/control errors. Normalized poll/epoll semantic and byte-stream traces match.
- Focused Autotools backend/linkage targets passed (3/3); native dependency audit passed. ASan/UBSan epoll and native HTTP/1 suites passed. Syscall traces prove ET/one-shot rearm and epoll use during public managed HTTP traffic.
- macOS C++20 native-core build, shared fake/poll contract (41), and native server (17) passed; the Linux-only suite reports exit 77. BSD, Windows, and other nonlocal lanes remain assigned to CI and the v3 PR.
- Changed-file cpplint, file-size, and warning-suppression checks passed. Repository complexity and duplication checks retain only their unchanged HEAD-baseline findings (three existing complexity offenders; two existing duplicate blocks); the new backend has no findings. Added direct `<mutex>`/`<string>` includes to peer policy and an explicit poll include to the WebSocket test fixture for Linux/transport-interface build compatibility.
- Reproducible logs, source fingerprints, scripts, and syscall traces: `/private/tmp/libhttpserver-task126-receipts.RiDelB`. Formal validation and lifecycle remain runner-owned; task status intentionally stays In Progress.
