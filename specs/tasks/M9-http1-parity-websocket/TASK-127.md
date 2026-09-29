### TASK-127: Implement BSD/macOS kqueue managed I/O backend

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Private operation I/O
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide BSD/macOS kqueue managed I/O backend for libhttpserver v3.0.

**Action Items:**
- [ ] Map operation contract onto kqueue read/write filters.
- [ ] Preserve unread data on EV_EOF and handle EV_ERROR.
- [ ] Run shared oracle suite on FreeBSD and macOS.

**Dependencies:**
- Blocked by: TASK-099, TASK-100
- Blocks: TASK-182

**Acceptance Criteria:**
- EOF with unread bytes, independent read/write interest and error paths pass on FreeBSD and macOS.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-014
**Related Decisions:** DR-V3-004

**Status:** Not Started
