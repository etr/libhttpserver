### TASK-194: Complete public TLS profiles, live rotation and ACME selection

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Public TLS authentication and credential generations
**Estimate:** L
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Install and exercise the full applicable TLS feature set through supported public native-server APIs.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-194, require `base_branch=v3`, refute credential/authentication/lifetime failures with RED tests before implementing, then Groundwork validation/finalization.

**Action Items:**
- [ ] Extend the provider-neutral public seam to mTLS trust/mode, eligible external-PSK profiles, live credential replacement and ACME publish/remove, reusing existing private registry/PSK runtime and secure owned-key behavior. Define callback/context/key lifetime without provider handles. Validate profile/ALPN combinations before bind/publication; reject unsupported combinations honestly.
- [ ] Add installed independent TCP client tests for SNI/default-host, mTLS request/require (valid, absent and untrusted client certs), TLS1.2/1.3 external PSK on HTTP/1, rejected identity and blocked callback timeout/cancellation. Bound lookup/handshake work and callback retirement; application callbacks must never make handler-safe stop synchronously join them.
- [ ] Add real public concurrent rotation tests: hold an established connection/handshake, publish a valid replacement, observe new handshakes using the new cert while old sessions remain usable; rejected publication preserves current generation. Exercise resumption policy and reject 0-RTT application data.
- [ ] Add public ACME certificate install/remove independent handshakes: exact SNI plus `acme-tls/1` only on TCP443, expiry, invalid SAN/digest/key rejection, removal, normal HTTP cert isolation and port/ALPN mismatch rejection. Preserve real selection predicate; no automatic issuance/client orchestration.
- [ ] Turn `test/examples/v3_tls_rotation.cpp` into an installed public example demonstrating actual listener use/rotation; extend installed witnesses and receipts, TLS usage/migration docs and header/dependency audits. Existing private tests remain supporting evidence.

**Acceptance Criteria:**
- All named public SNI/mTLS/PSK/rotation/ACME scenarios execute with independent clients and installed shared/static packages, with meaningful cert/profile/body assertions.
- Concurrent publication, blocked PSK callback, cancellation, drain and failed validation preserve generation safety, bounded work and exactly-once terminal/retirement; normal/ASan/TSan owner suites pass.
- TLS-off same declarations/typed unavailable operations, C++20/Groundwork validation, evidence, merge/cleanup pass. PSK remains HTTP/1-only; no false H2/H3/QUIC PSK or post-handshake client-auth claim.

**Dependencies:**
- Blocked by: TASK-193
- Blocks: TASK-195, TASK-197, TASK-185
**Related Requirements:** PRD-V3N-REQ-014, PRD-V3N-REQ-016, PRD-V3N-REQ-025, PRD-V3N-REQ-031, PRD-V3N-REQ-032, PRD-V3N-REQ-034, PRD-V3N-REQ-035, PRD-V3N-REQ-036, PRD-V3N-REQ-037, PRD-V3N-REQ-038.
**Related Decisions:** DR-V3-002, DR-V3-006, DR-V3-007, DR-V3-008.
**Status:** Not Started
