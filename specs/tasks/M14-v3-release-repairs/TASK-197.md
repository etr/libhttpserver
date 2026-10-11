### TASK-197: Complete public HTTP/3 lifecycle, policy and WebSocket parity

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Public H3/QUIC lifecycle and shared semantic services
**Estimate:** L
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Complete all in-scope public H3 behavior, including bounded duplex streams, drain, credentials and RFC9220, rather than stopping at a basic route.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-197, require `base_branch=v3`, root-cause any failing scenario and use C++20 RED/GREEN, then Groundwork validation/finalization.

**Action Items:**
- [x] Extend installed public witnesses to same-route H1/H2/H3 parity for hooks/errors, ordered fields, raw target/routing, authentication, forms/multipart and response definitions/files/borrowed leases. Verify peer-policy at QUIC admission and migrated-path validation, diagnostics and per-request metadata; no test-only dispatch context supplies missing public state.
- [x] Exercise parallel streams, paused upload/download/application resume, flow control, QPACK blocked headers, stream reset/STOP_SENDING, client disconnect, loss/reordering and credit/budget recovery. Ensure control capacity and healthy streams progress when data/stream/connection limits saturate.
- [x] Integrate native-server drain with staged H3 GOAWAY, rejected new work, accepted stream completion, WS Close and deadline cancellation; one server ticket counts all listeners/protocols without counting protocol linger as unfinished application work. Preserve QUIC closing/draining three-PTO alias/key and pending-datagram lifetime, and request_stop handler safety.
- [x] Add public RFC9220 independent WebSocket cases alongside HTTP requests, backpressure/close notifications, limits and cancellation. Reuse codec/session and existing independent predicate/corpus.
- [x] Exercise QUIC SNI, initial mTLS, credential rotation across existing/new connections, resumption with 0-RTT rejected and challenge isolation (ACME never selected for QUIC). Reject unsupported PSK/ALPN and post-handshake auth before listening/publication.
- [x] Extend public installed runner receipts to cover all scenarios, shared/static managed/external plus multiple TCP/UDP listeners. Align applicable native/public examples and current capability docs without replacing private conformance evidence.

**Acceptance Criteria:**
- Pinned aioquic and quic-go public-client matrices, RFC9220 and existing semantic/drain/transport closure predicates all pass against installed packages, with per-scenario evidence and no `not_supported` for supported configurations.
- Slow-peer windows/control progress and saturation/cancellation/resume obey existing TASK-180 thresholds; resource usage returns to the correct baseline and no handler/completion survives its owned exchange unsafely.
- Public TCP/QUIC TLS profile/rotation/isolation witnesses, normal/ASan/TSan owner tests, C++20/Groundwork validation and merge/cleanup pass; nonlocal checks remain CI-owned.

**Dependencies:**
- Blocked by: TASK-194, TASK-196
- Blocks: TASK-198, TASK-185
**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-009, PRD-V3N-REQ-011, PRD-V3N-REQ-012, PRD-V3N-REQ-013, PRD-V3N-REQ-015, PRD-V3N-REQ-017, PRD-V3N-REQ-021, PRD-V3N-REQ-023, PRD-V3N-REQ-024, PRD-V3N-REQ-025, PRD-V3N-REQ-026, PRD-V3N-REQ-027, PRD-V3N-REQ-028, PRD-V3N-REQ-029, PRD-V3N-REQ-030, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-033, PRD-V3N-REQ-034, PRD-V3N-REQ-035, PRD-V3N-REQ-036, PRD-V3N-REQ-038.
**Related Decisions:** DR-V3-001 through DR-V3-008.
**Status:** In Progress

**Implementation evidence:** [Local gate evidence](../../architecture/v3/TASK-197-implementation-evidence.md) and [versioned receipt](../../architecture/v3/TASK-197-receipt.json). All implementation action items are verified; validation/finalization remain runner-owned.
