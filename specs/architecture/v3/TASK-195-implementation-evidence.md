# TASK-195 implementation evidence

Implemented on `task/TASK-195` from clean v3
`eb4bf441a0aafc1a651959947f7a5f490d12dbdc`, reusing the registered
`.worktrees/TASK-195` and preserving its interrupted implementation and receipts.
The private ownership boundary is documented in [TASK-195-design.md](TASK-195-design.md).
The implementation commit is the commit containing this document. Caller
Groundwork review, task completion bookkeeping, archive, merge and cleanup
remain pending; this document claims implementation and local verification only.

Public certificate/mTLS TLS ALPN now selects the production native H2 owner,
using the same route table, hooks, policy, metadata, budgets and drain scope as
H1. One serial wire affinity and independent stream affinities run on configured
workers. Bounded semantic handoff, synchronized body rings, exact consumed
credit and affinity-ordered cancellation preserve running frames and late-signal
safety. Listener/connection/stream children enforce actual hierarchical limits.
Native deadlines cover idle SETTINGS/header, body, suspension, response/WS
output and drain; native drain retains staged GOAWAY and accepted output
retirement. PSK remains H1-only, public H3 stays unavailable, and public API
signatures remain unchanged. Required H1 parity exposed and repaired immutable
disconnect publication and a final lifetime-locked disconnect snapshot.

## Required local GREEN

Paths below are relative to `build/task195/`; machine hashes, commands, source
identity, exact scenario inventories and external artifact directories are in
[TASK-195-receipt.json](TASK-195-receipt.json) and `build/task195/handoff.json`.

| Gate | Result | Receipt |
| --- | --- | --- |
| Relevant TLS-on normal, ASan+UBSan, TSan suites | 76/76 in each lane, no failing suite | `normal-on-closed-suites.log`, `asan-on-green-suites.log`, `tsan-on-closed-suites.log` |
| Relevant TLS-off normal, ASan+UBSan, TSan suites | 20/20 in each lane | `{normal,asan,tsan}-off-closed-suites.log` |
| Installed public native H2, both pinned independent stacks | Each lane: shared/static × managed/external, all four cells; each cell 24 Python families/68 scenarios, 24 Node families/66 scenarios, three prebind predicates: 137 scenarios/cell | `public-{normal,asan,tsan}-green/receipt.json` |
| Installed TLS-off rejection and dependency audit | All four cells in all three lanes; no provider dependency | `public-{normal,asan,tsan}-off-green/receipt.json` |
| Installed package configuration, linkage and H1 traffic | All four cells, TLS-on/off, normal/ASan/TSan | `package-{normal,asan,tsan}-{on,off}-closed2/receipt.json` |
| Explicit public H2 selection plus supplemental private protocol interoperability | Both pinned stacks, five exact private protocol scenarios each; h2spec 145/145 required cases, two unchanged named RFC9113 exclusions | `protocol-green/receipt.json` |
| Fail-closed runner/package contracts | Public H2 6, installed consumer 27, package 35, workflow 4, H2 gate 6 tests; all pass | `public-contracts-complete2.log`, `package-consumer-green.log`, `package-green.log`, `package-workflow-green.log`, `h2-gate-contracts-green.log` |
| Headers, examples, native linkage, README/release docs, actual Doxygen, hygiene and install layout | TLS-on/off pass; real Doxygen with Graphviz disabled because absent, no warning filters changed | `check-local-{on,off}-complete.log`, `layout-{on,off}-complete.log` |
| Changed C++ cpplint, Python/Node syntax and whitespace | Pass | `cpplint-complete.log`, `public-contracts-complete2.log`, `whitespace-complete.log` |
| Source distribution membership | New owner/tests/tooling/example/evidence present | `dist-complete.log`, `dist-membership.json` |

Clang18/C++20 and the selected OpenSSL3.5.9 provider instrument both library and
installed consumers. Independent clients are Python hyper-h2 4.4.1 and Node
24.15.0/nghttp2 1.68.0; CA/hostname verification is enabled. Public compilation
uses installed public headers outside the repository, with audited dependency
files, link maps, shared/static graphs and exact library/header/tool/fixture
hashes. All three TLS-on matrices validate their complete inventory and unchanged
covered inputs. The public example is also installed, compiled and exercised.

ASan/UBSan runs outside ptrace with leak detection, stack-use-after-return/scope
and nonrecovering diagnostics. TSan uses halt-on-error and no new suppressions.
The established named request-lifecycle allocation-count-only TSan exclusion
remains (one internal allocation probe); normal/ASan retain the full probe.
The executable-specific Clang18 shared-libsan setup executes every external
readiness allocator probe: 27 tests/491 checks, no skipped checks. Supplemental
H3 exchange/drain suites protect the shared serialized handler helper without
claiming public H3 support.

## Causal closure

The journal `.debug/task195/journal.md` and all original task-local receipts
remain intact. Each design doubt has a concrete discriminator and final proof:

| Doubt | Discriminator and closure |
| --- | --- |
| Public listen succeeds but fixture supplies H2 | `public-behavior-red.log`/`public-red.json` retained baseline `not_supported`; final installed public-only graph and both client matrices prove real ALPN/routing. |
| Handler occupies wire domain or unsafe cancellation | `native-owner-red.log` running-resume cancellation failure; `http2_native_owner` and `http2_dispatch` GREEN plus both stacks' held-running/suspended/upload/response sibling/PING cases. |
| Shared body/failure/credit state races | `body-race-red.log`, `body-failure-race-red2.log`, `credit-valid-red.log`; synchronized ring/failure tests and exact consumed/padded/reset credit cases pass normal/ASan/TSan. |
| Reset frees queued/running frames or late work re-enters | Native owner entry/destructor barriers and both installed clients' running reset, queued reset, EOF, late-signal cases; hook completion counts and final zero accounting. |
| Resource hierarchy is only nominal or handoff grows | `hierarchy-red.log`, body/header staging RED; child-scope unit accounting and six installed saturation families (connection/timer/stream/header/body/output), control progress and recovered baselines. |
| Idle timers fail or unrelated traffic slides anchors | `body-idle-red.log`, `header-release-red.log`; admission/body and completed-header regression GREEN, idle SETTINGS/header and stream/WS deadlines in every installed cell. |
| Output borrow or compression continuation invalidated | Existing partial-output/failure/CONTINUATION corpus and flow/fair-output/conformance suites retained, plus accepted response/body completion through staged native drain. |
| WS monopolizes connection or closes twice | Both stacks' real RFC8441 negotiation, echo+sibling/PING, bounded backpressure, reset/deadline and exact close-count cases. |
| Native drain double counts, finishes early, or allows handler wait | Both stacks assert counted-handler `would_deadlock`, request ACK, both GOAWAY stages, cutoff/accepted completion, no-waiter deadline and retained ticket after server destruction. |
| ALPN/live policy violates profiles | `cbc-red.log` prohibited CBC served200; fixed pre-hook cipher rejection; enabled-version publication, H2-only, mTLS, disabled-H2/PSK rejection and retained ACME/TLS suite proof. |
| External stale registrations restart retired work | Both clients capture a live connection generation, wait for full EOF retirement, filter still-live listener registrations and replay the retired generation twice while active; zero new hooks. Stopped driver separately returns `invalid_state`. Rollback and mixed-listener public predicates also pass. |
| Shared H1 shutdown publishes partial disconnect metadata | Exact `.debug/task195/tsan-h1-parity-original.log`; one equivalent retained TASK-194 baseline replay passes without disproving the timing race. `h1-retirement-red2.log` causally exposes reason publication. Minimal atomic publication/lifetime snapshot repair passes all required H1/H2 normal/ASan/TSan gates. |

Fixture failures remain distinct from library fixes. Node drain-finish keeps a
second accepted upload open through both required GOAWAY stages because
node:http2 otherwise retires its last completed stream before the barrier PING;
both stages and accepted completion remain asserted. A clock discriminator
recorded wall=15307ms versus monotonic=9737.8402ms at the false 12s SETTINGS
observation failure, before the native 10s deadline. Node bounds now use
`performance.now()` with identical durations and wire assertions. The initial
ASan H1 nonce expiry failure and negative wall-clock durations are preserved;
unchanged replay and the complete final 76-suite ASan run pass.

One original ASan installed fixture stack receipt was overwritten before this
resume. The journal honestly retains the observed driver-reference
stack-use-after-scope diagnosis and the lost-receipt limitation; the independent
original external crash JSON and corrected ASan replay remain. No original
stack is reconstructed or represented as preserved.

BSD/macOS/Windows and other nonlocal platform checks are unexecuted here and
owned by CI/the v3 PR under AGENTS.md. TASK-200 timer/manual-executor teardown,
public H3, benchmarks, release orchestration and unrelated worktrees remain
outside this implementation. No local required gate remains incomplete.
