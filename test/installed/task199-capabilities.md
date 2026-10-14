# Current packaged public capabilities — 2026-10-13

TASK-199 executed both adapters from a fresh distribution extraction without Git
metadata. All public cells compile C++20 against the selected staged headers and
shared/static library, outside source/build trees. Private engine suites provide
no success witness for these adapters. The [machine receipt](../../specs/architecture/v3/TASK-199-implementation-receipt.json)
and [implementation evidence](../../specs/architecture/v3/TASK-199-implementation-evidence.md)
retain exact inputs, commands and independent validation results.

Archive SHA-256: `75cfe738dd0857f4be9c347d9db36c170007c67dc4dbcfb47c8ca9f25b5f50a1`. Retained source:
`/tmp/task199-extract4/libhttpserver-3.0.0`. Each TLS lane has four shared/static × managed/external
cells for H1/RFC6455, TLS profiles, H2 and H3. TLS-on executes 328
credential/profile scenario rows, 548 H2 scenario rows and
64 independent H3 basic rows plus
192 H3 configuration/lifecycle rows and
204 nested credential/semantic rows.

| Applicable requirements | Current installed public evidence |
| --- | --- |
| PRD-V3N-REQ-001–003 | Strict TLS-off/on metadata, dependency, header and complete archive-symbol audits; no MHD; selected provider only |
| PRD-V3N-REQ-004 | Byte-exact H1.0/H1.1, POST, persistent/pipelined traffic and cleanup in all eight cells |
| PRD-V3N-REQ-005–008 | Pinned hyper-h2/Node and aioquic/quic-go exchanges; exact multiplexing/flow/cancellation inventories |
| PRD-V3N-REQ-009 | Public H1/H2/H3 semantic replay over installed native mixed listeners |
| PRD-V3N-REQ-010–013 | Installed H1 upgrade, text/binary/fragmentation, refusal, close/cancellation and bounded pressure; RFC8441/RFC9220 wire corpora and lifecycle |
| PRD-V3N-REQ-014–016 | Existing invalid/missing-credential/rollback negatives preserved; supported TLS/H2/UDP-H3 bind and exchange; both loop modes and stale-generation rejection |
| PRD-V3N-REQ-017–030, as applicable | Ordered headers/trailers/raw targets, auth/forms/multipart, body admission, suspension/reset, sources/leases and reusable response definitions; complements existing API unit checks |
| PRD-V3N-REQ-031–033 | Handler-safe stop, decoded cancellation, retained aggregate drain tickets/deadlines and WebSocket retirement |
| PRD-V3N-REQ-034–036 | Certificate/SNI/mTLS/eligible HTTP1 PSK, held/existing/new-session rotation and authentic TCP443 ACME; QUIC credential isolation |
| PRD-V3N-REQ-037 | Same installed declarations and typed encrypted rejection before binding in TLS-off |
| PRD-V3N-REQ-038, as applicable | Current public semantic/TLS/policy replays; complete release parity, including remaining historical feature gates, remains TASK-185 owned |

The complete benchmark matrix passes as reduced integration evidence: four
TLS-off and sixteen TLS-on cells, 16 measured requests per cell, 320 measured
requests total. Defaults for a full benchmark remain 4096 requests, payload 1024,
four workers, H1/H2 concurrency eight and H3 concurrency four. Independent
validation rejects use of this reduced workload as full performance acceptance.

Both package lanes pass native linkage/header/hygiene/fixture checks, strict
independent audits, and identical effective declarations for all 41 installed
headers. All 138 focused contract tests pass. Independent disposable copies
fail for scenario omission, changed process readiness, changed statistics,
different expected archive/library identity and reduced-as-full classification.
Original accepted evidence is unchanged.

This is current evidence, separate from historical TASK-179 release inventories
and receipts. TASK-182 was already Complete in its card and index; that status is
preserved. TASK-185 runner wiring, complete release acceptance and existing local
sanitizer/resource requirements remain open. BSD/macOS/Windows checks are
unexecuted locally and remain CI/v3-PR owned.
