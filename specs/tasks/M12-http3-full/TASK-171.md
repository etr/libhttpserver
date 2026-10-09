### TASK-171: Implement HTTP/3 WebSocket Extended CONNECT negotiation

**Milestone:** M12 - Full HTTP/3
**Component:** WebSocket codec and adapters
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide HTTP/3 WebSocket Extended CONNECT negotiation for libhttpserver v3.0.

**Action Items:**
- [x] Require peer SETTINGS_ENABLE_CONNECT_PROTOCOL.
- [x] Validate CONNECT pseudo-headers and registered WS route.
- [x] Reject unsupported protocols and premature CONNECT.

**Dependencies:**
- Blocked by: TASK-121, TASK-161, TASK-169
- Blocks: TASK-172

**Acceptance Criteria:**
- Only peer-enabled CONNECT on a registered route upgrades the selected request stream; extension offers receive no negotiation.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-011, PRD-V3N-REQ-012
**Related Decisions:** DR-V3-001, DR-V3-003

**Status:** Complete

**Implementation evidence:**

- Peer ENABLE_CONNECT_PROTOCOL is validated and published atomically; opening eligibility is immutable through frame fragmentation and QPACK blocking.
- Registered CONNECT routes explicitly call upgrade; successful admission owns one session and one stream-local 200 HEADERS section without FIN or negotiated extensions. Premature/malformed CONNECT resets only its stream; typed handshake/storage refusals preserve ordinary responses.
- C++20 TLS-off: 20 suites / 214 cases / 5,550 checks passed. TLS-on with OpenSSL 3.5.9: 12 suites / 145 cases / 2,586 checks passed. ASan/UBSan: 6 suites / 80 cases / 1,746 checks passed (LeakSanitizer disabled under ptrace).
- `make -j6 check-local`, changed-file cpplint and `git diff --check` passed. Nonlocal checks remain assigned to CI/v3 PR.
- See [negotiation behavior and verification](../../../docs/task-171-http3-websocket-negotiation.md). DATA transport and close lifecycle remain TASK-172; validation, commits and finalization remain runner-owned.
