### TASK-195: Route negotiated public HTTP/2 streams through native listeners

**Milestone:** M14 - v3 local release readiness repairs
**Component:** TCP ALPN dispatch and HTTP/2 connection owner
**Estimate:** L
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Real `h2` negotiation through the public server serves independent streams using the existing route/exchange/hooks/budget/lifecycle services.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-195, require `base_branch=v3`, design/refute owner/executor boundaries and C++20 RED/GREEN tests, then Groundwork validation/finalization.

**Action Items:**
- [ ] Add failing installed public-only h2 clients using pinned Python-h2 and Node clients; the same route table must serve H1/TLS and H2 by ALPN. No cleartext h2c or private fixture can satisfy this test.
- [ ] Add a production TCP H2 owner composing existing `http2_request_engine` feed/output/error interfaces with TLS operations, fair owner serialization and transport timers. Reuse protocol owners; do not transplant fixture routes, observers or custom polling loops. Respect engine requirement that state-changing handler effects return to its serialization domain while independent streams progress on configured workers.
- [ ] Integrate metadata, route_sync cap, ordered fields/trailers, body admission, resume signals, streaming sources/overlays, hooks, peer-policy and diagnostics. Apply listener/server/connection/stream/header/body/output budgets and release consumed credit correctly.
- [ ] Add public multiplex tests: a suspended/slow upload or response stream must not block a healthy stream/control traffic; reset/disconnect cancels only affected work, restores capacity and prevents late completion use-after-free. Exercise RFC8441 WebSocket alongside ordinary streams, bounded backpressure and close once.
- [ ] Connect native stop/drain to staged H2 GOAWAY and deadline timers; reject new streams, finish accepted streams, cancel remaining requests/WS at deadline, retain tickets after server destruction and reject counted-handler waits. Test managed/external readiness, stale events and multi-listener rollback.
- [ ] Update installed H2 expectations/receipts, examples/docs and independent gate fixture selection to exercise public installed servers where the required predicate is public capability. Keep private protocol corpus and pinned h2spec coverage as additional evidence.

**Acceptance Criteria:**
- Installed shared/static managed/external public listeners pass both independent clients' real h2 round trips, parallel streams, request/response bodies/trailers, header-time decisions/resume, cancellation, RFC8441, GOAWAY and deadline cases.
- Saturating a configured limit refuses bounded work without stalling controls/other streams; cancellation/retirement returns resource usage to the correct baseline. Hook/policy behavior matches equivalent H1 routes.
- H2 corpus/h2spec, relevant normal/ASan/TSan owner and public-consumer tests, TLS-off rejection, C++20/Groundwork validation and merge/cleanup pass.

**Dependencies:**
- Blocked by: TASK-190, TASK-194
- Blocks: TASK-196, TASK-198, TASK-185
**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-006, PRD-V3N-REQ-009, PRD-V3N-REQ-011, PRD-V3N-REQ-012, PRD-V3N-REQ-013, PRD-V3N-REQ-015, PRD-V3N-REQ-017, PRD-V3N-REQ-021, PRD-V3N-REQ-023, PRD-V3N-REQ-024, PRD-V3N-REQ-025, PRD-V3N-REQ-026, PRD-V3N-REQ-027, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-033, PRD-V3N-REQ-038.
**Related Decisions:** DR-V3-001, DR-V3-003, DR-V3-004, DR-V3-005, DR-V3-006, DR-V3-008.
**Status:** Not Started
