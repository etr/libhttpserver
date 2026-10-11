### TASK-196: Add production UDP QUIC ownership and public HTTP/3 routing

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Native UDP listener, QUIC connection owner and HTTP/3 dispatch
**Estimate:** L
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Public native-server configuration binds UDP and completes a real authenticated HTTP/3 route through production QUIC/H3 owners.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-196, require `base_branch=v3`, compare/refute public listener and connection ownership choices, C++20 RED/GREEN, then Groundwork validation/finalization. See `../v3-release-integration-design.md`.

**Action Items:**
- [x] Define an explicit provider-neutral endpoint/protocol mapping consistent with existing TCP listener syntax and pre-bind validation. A configured H3 endpoint must bind UDP, report the actual port, retain separate TCP/UDP identities and roll back partial binds. Document whether one logical endpoint owns both transports; avoid silent TCP-only success for H3.
- [x] Add failing installed public-only aioquic/quic-go handshake+route witnesses in managed/external modes. Use public credential installation and route registration; no detail includes or fixture process counts as product transport.
- [x] Build a production bounded serialized QUIC network owner around mature `quic_server_admission`, datagram dispatch, TLS session, identity, flow/recovery/storage, connection lifecycle and `http3_request_engine`. Extract reusable protocol composition from `test/support/http3_network_owner.*` only after removing test-only file serving, capture/key observers, manual executor, fixture constants and behavior profiles; have remaining test fixtures consume production ownership where suitable rather than copying the test owner wholesale.
- [x] Integrate UDP operation ownership/generations, Initial/Retry/amplification admission, CID registrations, actual send completion debits, TLS/ACK/PTO/pacing/deadline scheduling and early cancellation. Bind server/listener budgets and public credentials/routes/hooks/policy from the native-server pimpl. Retain buffers and aliases through pending submissions and protocol close expiry.
- [x] Make a basic H3 route, upload/response/trailers and two parallel streams work over the actual public listener; include server destruction and failed handshake cleanup. Update only now-supported H3 installed configuration expectations and distributed production source manifests.

**Acceptance Criteria:**
- Both independent H3 clients authenticate and execute installed public routes over UDP for shared/static managed/external packages; observed body/trailers and protocol metadata match registered routes.
- Bad Initial, rejected policy, timeout, partial bind, cancellation and stop remain bounded and release storage/CID registrations after the correct completion/expiry; no raw test support dependency enters production.
- Existing QUIC transport/capture/corpus and applicable TLS/H3 normal/ASan/TSan tests pass; TLS-off same-source typed rejection, C++20/Groundwork validation and merge/cleanup complete.

**Dependencies:**
- Blocked by: TASK-191, TASK-193, TASK-195
- Blocks: TASK-197, TASK-185
**Related Requirements:** PRD-V3N-REQ-003, PRD-V3N-REQ-007, PRD-V3N-REQ-008, PRD-V3N-REQ-009, PRD-V3N-REQ-014, PRD-V3N-REQ-015, PRD-V3N-REQ-016, PRD-V3N-REQ-021, PRD-V3N-REQ-025, PRD-V3N-REQ-027, PRD-V3N-REQ-034, PRD-V3N-REQ-037.
**Related Decisions:** DR-V3-001, DR-V3-002, DR-V3-004, DR-V3-006, DR-V3-007, DR-V3-008.
**Status:** In Progress

**Implementation evidence:** [TASK-196-design.md](../../architecture/v3/TASK-196-design.md) documents ownership and bounded lifecycle decisions. [TASK-196-implementation-evidence.md](../../architecture/v3/TASK-196-implementation-evidence.md) and [TASK-196-receipt.json](../../architecture/v3/TASK-196-receipt.json) record completed local native/public/capture/sanitizer gates and exact proof reuse scope. The pinned simulator clock compatibility and original failed attempts remain explicit. Caller-owned Groundwork validation/finalization, merge and cleanup remain pending; task status stays In Progress. Nonlocal BSD/macOS/Windows execution belongs to CI and the v3 PR.
