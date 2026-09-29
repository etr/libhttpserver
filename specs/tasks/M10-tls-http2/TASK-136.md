### TASK-136: Implement ACME TLS-ALPN-01 publication and removal

**Milestone:** M10 - TLS and HTTP/2
**Component:** TLS adapter and credential registry
**Estimate:** L
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide ACME TLS-ALPN-01 publication and removal for libhttpserver v3.0.

**Action Items:**
- [ ] Validate RFC 8737 challenge SAN, OID, digest and key match.
- [ ] Publish/remove short-lived challenge snapshots.
- [ ] Select only exact TCP 443 SNI with sole acme-tls/1 ALPN.

**Dependencies:**
- Blocked by: TASK-131, TASK-132
- Blocks: TASK-147

**Acceptance Criteria:**
- Only exact TCP 443 SNI plus sole acme-tls/1 ALPN selects a validated challenge certificate; removal is safe.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-036
**Related Decisions:** DR-V3-002, DR-V3-007

**Status:** Not Started
