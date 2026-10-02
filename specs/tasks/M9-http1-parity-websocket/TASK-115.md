### TASK-115: Port RFC 7616 Digest authentication and replay checks

**Milestone:** M9 - HTTP/1 parity and WebSockets
**Component:** Authentication and forms
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide RFC 7616 Digest authentication and replay checks for libhttpserver v3.0.

**Action Items:**
- [x] Port Digest challenge, nonce and replay ledger without MHD.
- [x] Support documented algorithms with bounded parsing.
- [x] Test stale, replayed, malformed and valid independent-client flows.

**Dependencies:**
- Blocked by: TASK-114
- Blocks: TASK-128

**Acceptance Criteria:**
- Independent client challenge/response passes and stale/replayed nonce cases fail without MHD.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-002, PRD-V3N-REQ-038
**Related Decisions:** DR-V3-001

**Status:** Complete
