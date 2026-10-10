### TASK-193: Install public credentials and serve native TCP TLS

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Public TLS credential configuration and TCP listener ownership
**Estimate:** L
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** An installed public-only application installs certificate credentials and serves real HTTP/1 over TLS through `native_server` in managed/external modes.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-193, require `base_branch=v3`, apply design-it-twice/doubt-driven-development to the public credential seam, implement with verified C++20 RED/GREEN, then validate/finalize. `../v3-release-integration-design.md` records the recommended direction, not frozen signatures.

**Action Items:**
- [ ] Design the smallest stable provider-neutral public credential/configuration API backed by the mature `tls_credentials_registry` snapshots. Associate credentials with listener/host selection, define ownership, pre-listen validation, replacement atomicity and TLS-off typed outcomes. Hide provider/context/key/OS types; do not expose detail headers or make applications create a provider `SSL_CTX`.
- [ ] Add failing installed public consumer tests for credential installation, missing/invalid credentials failing before bind, SNI/default selection, TLS-off same-source rejection and a real independent TLS HTTP/1 round trip. Test partial multi-listener bind rollback.
- [ ] Integrate existing `tls_socket_backend`/`tls_io_backend`, bounded handshake runtime and `connection_engine` into native TCP accept ownership. Retain snapshot selection through handshake, pending completions and teardown; implement handshake deadlines, safe cancellation, bounded admission and stop/drain. Peer policy rejects before TLS/application work and hooks observe genuine connection metadata.
- [ ] Replace only proven TLS listener refusal paths after real transport works. Preserve disabled protocol rejection. Use one serialized connection owner and no application hook/callback under an I/O lock.
- [ ] Start a packaged public-capability client/receipt runner using existing installed-package tooling, with public headers/pkg-config/linkage proof and independent TLS client identity. Update TLS configuration expectations in `v3_package_consumer.cpp`, `v3_package_matrix.py` and their contracts; keep H2/H3 expectations accurate until their tasks land. Convert the TLS rotation example's credential entry points to public API where applicable and document supported installed usage.

**Acceptance Criteria:**
- Installed shared/static public-only clients serve validated HTTP/1.0/1.1 TLS requests with selected certificate/SNI, managed/external readiness, and no private header/compilation macro or unapproved production dependency.
- Invalid credentials/options reject atomically before accept; handshake timeout/disconnect/stop/drain settle pending operations once and release connection/storage budgets.
- TLS-off/on public declaration audits, plaintext regression suites, relevant TLS lifecycle normal/ASan/TSan tests, C++20 and Groundwork validation pass; merge/cleanup complete.

**Dependencies:**
- Blocked by: TASK-192
- Blocks: TASK-194, TASK-195, TASK-196, TASK-185
**Related Requirements:** PRD-V3N-REQ-001, PRD-V3N-REQ-002, PRD-V3N-REQ-003, PRD-V3N-REQ-004, PRD-V3N-REQ-014, PRD-V3N-REQ-015, PRD-V3N-REQ-016, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-034, PRD-V3N-REQ-037.
**Related Decisions:** DR-V3-001, DR-V3-002, DR-V3-004, DR-V3-006, DR-V3-007, DR-V3-008.
**Status:** Not Started
