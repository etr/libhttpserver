### TASK-127: Implement BSD/macOS kqueue managed I/O backend

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide BSD/macOS kqueue managed I/O backend for libhttpserver v3.0.

**Action Items:**
- [x] Map operation contract onto kqueue read/write filters.
- [x] Preserve unread data on EV_EOF and handle EV_ERROR.
- [x] Run the shared oracle suite locally on macOS and record FreeBSD coverage for CI/v3 PR (AGENTS.md platform policy, 2026-10-06).

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-182

**Acceptance Criteria:**
- EOF with unread bytes, independent read/write interest and error paths pass on FreeBSD and macOS.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014
**Related Decisions:** DR-V3-004

**Status:** Complete


**Implementation verification (2026-10-06):**
- macOS/FreeBSD managed mode selects private kqueue; external mode retains the independent poll adapter. Separate persistent `EV_DISPATCH` read/write filters preserve latent readiness and buffered EOF. A separate `EV_RECEIPT` control phase distinguishes successful zero-data receipts from registration failures.
- macOS arm64 C++20 Autotools library build and 11 focused targets passed: 245 littletest cases, including 48 kqueue cases, with zero skips/failures. Poll/kqueue normalized semantic traces and 262144-byte backpressure streams matched. Managed native HTTP traffic produced real kqueue wait/event receipts.
- Four native-core consumers passed ASan/UBSan and TSan (kqueue, shared fake/poll contract, native server, connection engine). Changed-file cpplint, public-header gates, native linkage audit, file-size, warning-suppression, assertion, and fence checks passed. Complexity/duplication match the unchanged HEAD baseline: three existing complexity offenders and two existing duplicate blocks; none introduced.
- Shared managed socket ownership/steps were extracted from epoll to avoid duplicate cancellation, buffer/fd lifetime, incarnation, and completion logic. Epoll's private probes now address the shared base; Linux epoll execution after this refactor is unexecuted locally and assigned to CI/v3 PR with FreeBSD, Windows, and other nonlocal checks. Actual FreeBSD runtime acceptance is not claimed.
- Details: [implementation evidence](../../architecture/v3/TASK-127-implementation-evidence.md). Reproducible logs and source fingerprints: `/private/tmp/task127-receipts`. Formal validation, commits, task completion, merge, and cleanup remain runner-owned; status stays In Progress.
