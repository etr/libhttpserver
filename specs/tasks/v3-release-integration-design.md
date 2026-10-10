# Public integration design recommendation

This is a bounded design comparison for task planning. TASK-193/195/196 planners must settle exact public signatures and implementation ownership with TDD before changing the API.

## Framing and alternatives

Dominant use: an installed C++20 application configures listeners/credentials, registers one `task<void>(exchange&)` route and serves TCP H1/H2 and UDP H3 with common hooks, policy, limits and lifecycle. Public headers cannot expose provider/OS/detail types. Existing real protocol engines and credential snapshots must be reused. `native_server` currently rejects all TLS/H2/H3 in `prelisten_ready`; listener records hold only H1 `connection_engine`. H2/H3 engines expose private composition seams. QUIC's most complete network composition is test-only `test/support/http3_network_owner.*`, so it is evidence for a carefully extracted production owner, not a production library dependency.

| Design | Depth | Locality | Seam placement | Assessment |
|---|---|---|---|---|
| One `native_server` facade owns protocol-specific private listeners/connection owners; provider-neutral credentials feed private immutable snapshots | High: few public entry points hide admission, protocol negotiation, timers and retirement | Credential and lifecycle fixes concentrate in registry/native owner; wire fixes remain protocol-local | TCP ALPN and UDP admission below the same exchange, with owner-serialized stream effects | Recommend |
| Public pluggable transport/session factories with application-supplied handshake/event adapters | Lower: application must understand lifetime, flow control and provider handoff | Every client repeats protocol integration and credential wiring | Exposes current private implementation pressure to installed callers | Reject: contradicts no pluggable backend and simple common API |
| Separate public H1/TLS/H2/H3 server classes with their own credential/lifecycle wrappers | Medium per protocol, wide total API | Hooks/policy/drain and route behavior drift across wrappers | Boundaries divide shared semantics instead of actual wire state | Reject: violates one-route design and duplicates lifecycle responsibilities |

Choose the first design. Keep per-protocol owners private and genuinely responsible for scheduling, flow/credit, transport completion and shutdown rather than thin wrappers around test fixtures. Credential value/configuration and replacement/publication are the only new user concepts needed; keep immutable generation/provider details private. Public listener configuration must explicitly define TCP/UDP binding and validation, and preserve existing plaintext call sites. Use existing stable endpoints where the contract suffices; add a small provider-neutral transport selector only if a single boolean TLS flag cannot unambiguously express the needed mapping.

## Doubt register to clear before commits

Decision: compose the existing owned wire engines behind one native-server lifecycle and publish provider-neutral credentials into immutable private generations.

| Failure mode / disproof | Required evidence |
|---|---|
| Success means bound socket but no actual negotiated protocol route | Installed public-only independent TLS/H2/H3 requests validate protocol, certificate, response body and trailers; source/private includes rejected |
| Handlers run concurrently against an owner-only H2/H3 engine | Suspended parallel streams, resets, timer races and handler completions under TSan; engine effects serialize in one owner domain |
| Credential replacement frees a context/PSK callback during a handshake or pending completion | Held handshake/connection across replacement, invalid publication preservation, late callback/cancellation tests and ASan/TSan |
| Public registry is global or one listener's policy selects another listener's key | Multiple listeners/hosts, default/SNI/mTLS/PSK and ACME isolation tests |
| Native drain counts QUIC linger forever or frees it before protocol expiry | Accepted-stream completion versus deadline tests plus pending-send/CID/key three-PTO retention and post-server ticket tests |
| External loop drops handshake/PTO/wakeup or reuses stale UDP generations | Managed/external public matrices, stale-event/cancel/rearm tests, loss/reorder independent clients |
| Resource saturation blocks control traffic or leaks reservations | Existing TASK-180 thresholds, stalled stream plus healthy/control stream, cancellation/resume usage baseline |
| Test-owner extraction smuggles fixture paths/observers/crypto/runtime dependencies into production | Dependency/header/package audits and test fixtures rewritten to consume production owner; no `test/` include or key-capture/file-root behavior in production |

These are open implementation obligations, not claims cleared by this planning document. No interface/concurrency commit proceeds with an untested open doubt; choose a smaller safe seam if a proposed boundary is refuted. No new rollout flag is needed merely for local unreleased v3 work; typed pre-bind validation and branch/worktree isolation provide reversible implementation.
