### TASK-191: Repair independent HTTP/3 semantic drain completion

**Milestone:** M14 - v3 local release readiness repairs
**Component:** HTTP/3 and QUIC drain ownership
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Resolve quic-go `drain deadline expired` and the retained unexpected shutdown traffic event without weakening semantic/closure predicates.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-191, require `base_branch=v3`, bounded failure reproduction and C++20 RED/GREEN, then Groundwork validation/finalization.

**Action Items:**
- [x] Read `task185-h3-supplement-05` semantic logs/captures and reproduce only the failing quic-go semantic drain lane. Follow `http3_udp_fixture.cpp` drain command/future and `test/support/http3_network_lifecycle.cpp`, request-engine GOAWAY, control-stream ACK retirement and QUIC closing timers. Do not assume timeout means a slow client or that all shutdown traffic is semantically wrong.
- [x] Add deterministic failing regression for the observed ownership/deadline trigger and an independent-client regression. Assert staged GOAWAY IDs, accepted request completion, no new admission, ticket completion versus expiry, exactly-once cancellation and proper protocol-period alias/key retention.
- [x] Repair owner/protocol/fixture lifecycle or runner event classification according to cause. Preserve immutable receipts, authenticated closure predicates, control traffic progress, bounded deadlines and the distinction between HTTP drain completion and QUIC transport retirement. If additional frames are required after semantic completion, label them accurately rather than accepting all traffic.
- [x] Run pinned aioquic and quic-go semantic clients and required H3/QUIC lifecycle tests. Reuse fixed composition later in the public transport tasks; do not create a second divergent owner implementation.

**Acceptance Criteria:**
- Both independent semantic clients complete the original drain scenario within its existing deadline; aggregate reaches the subsequent RFC 9220/fuzz stages.
- Deterministic H3 drain/QUIC close tests pass normal and ASan/UBSan; completed/expired drain outcomes, reset cancellation and pending-send lifetime remain correct.
- Existing transport capture/closure and RFC 9220 predicates pass unchanged; C++20/Groundwork validation and merge/cleanup complete.

**Dependencies:**
- Blocked by: TASK-190
- Blocks: TASK-192, TASK-196, TASK-185
**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-025, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-033.
**Related Decisions:** DR-V3-001, DR-V3-006, DR-V3-008.
**Status:** In Progress
