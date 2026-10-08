### TASK-162: Run independent HTTP/3 client smoke tests

**Milestone:** M11 - First HTTP/3 slice
**Component:** Build/packaging/validation
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide independent HTTP/3 client smoke tests for libhttpserver v3.0.

**Implementation scope:**
TASK-161 provides the private semantic engine, not a UDP/TLS endpoint. This task
includes the bounded test-only connection owner needed to compose the existing
QUIC admission, packet protection, TLS, recovery, flow control and HTTP/3 engines
on loopback. Both independent clients must use this real library composition;
an external HTTP/3 server or an in-memory framing test is not acceptance evidence.
The public listener's current HTTP/3 rejection does not block planning this test
endpoint. TASK-163 packages the endpoint for interop-runner use later; it is not
a prerequisite for TASK-162. Public listener enablement remains separate.
Implementation details are persisted in `.groundwork-plans/TASK-162-plan.md`
in the selected worktree. Existing acceptance criteria remain unchanged.

**Action Items:**
- [x] Run two independent HTTP/3 clients against GET and POST routes.
- [x] Exercise basic concurrent streams and typed failures.
- [x] Retain packet and TLS diagnostics for failures.

Implementation evidence and reproduction commands:
`docs/task-162-http3-evidence.md`. Status remains In Progress pending caller-owned
validation and finalization.

**Dependencies:**
- Blocked by: TASK-161
- Blocks: None

**Acceptance Criteria:**
- Two independent clients complete TLS handshake, h3 GET/POST and basic concurrent streams.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-009
**Related Decisions:** DR-V3-001

**Status:** Complete
