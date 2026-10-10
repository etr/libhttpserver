# TASK-194 public authentication and lifetime seam

Decision: publish provider-neutral auth generations into the existing registry; separate bounded application lookup retirement from native provider-step retirement.

The dominant installed application supplies host material/trust, initial client auth or HTTP/1 external PSK, attaches a shared store, rotates while serving and publishes/removes application-supplied ACME material. Provider handles and application executors remain private.

| Alternative | Depth | Locality | Seam |
| --- | --- | --- | --- |
| A: extend existing value/store, opaque bounded lookup runtime and move-only secure key | Deep validation/publication and cancellation/retirement hidden behind stable values | Registry owns generations, native owns I/O, application owns lookup retirement | Matches immutable generations and callback ownership |
| B: create lookup pools implicitly on replace/server | Small surface hides unreturned work and replenishes stalled capacity | Rotation/stop are coupled to arbitrary application code | Violates shared fixed capacity |
| C: provider contexts/executors/job tickets | Wide surface repeats sensitive integration in each application | Ownership duplicated across consumers | Leaks provider/transport boundary |

Choose A. Four existing host aggregate fields stay first; defaulted trust/profile/auth/PSK follow. Lookup runtime options govern only application workers/queue; native provider work keeps its server-owned runtime. Secure keys reuse existing provider-independent owned secure storage. TLS-off exposes identical API and typed unavailable runtime/credential/ACME operations.

Each callback borrows identity/context for its invocation and owns copies it retains. Mutable captured state requires synchronization. Application state must be owned through actual callback retirement. Stop/destruction requests cancellation without joining arbitrary code. Deadline-bound explicit retirement reports incomplete callbacks and refuses callback self-wait. Ignored cancellation retains capacity until callback returns. Late keys are wiped and cannot authenticate.

Every listener binds one exact profile/resolved initial-auth mode and enabled H1 policy. The private registry writer transaction covers binding registration and publication together. Concrete lease values constrain all host candidates, not application callbacks under locks. Live mismatch rejects atomically. Admission shutdown releases its lease; already-selected sessions retain their generation. Multiple compatible listeners retain independent leases. Normal and ACME writers use that same serialized private seam and the mature private registry transaction.

ACME publication validates material independently of binding location. Selection remains exact canonical SNI, sole acme-tls/1, TCP and authentic local port443. No public test-port override, ordinary challenge ALPN or automatic issuance.

## Doubts requiring evidence before commit

1. Profile ignored / live downgrade: public prebind/live mismatch, multiple leases and concurrent bind/replace.
2. Callback borrowed state UAF / late key authentication: held owned sentinel, timeout/cancel before release, sanitizers.
3. Rotation replenishes capacity: saturated fixed runtime and repeated publication, queue expiry and recovery.
4. Handler stop joins callback / shared stop disables server: completion acknowledgement while callback held, second server, explicit retirement/self-wait.
5. Mixed/stale selection: held accepted handshake, live replacement, actual cert/context generation.
6. ACME leakage: actual443/SNI/ALPN/port/expiry/remove/invalid material and normal HTTP isolation.
7. Resumption/0RTT bypass: independently controlled sessions, replacement/host isolation, attempted early bytes, zero early handler count.
8. Installed provenance: same-source off/on, shared/static and provider-independent server headers.

All doubts initially open. No implementation commit until exact durable receipts clear them; unresolved dependencies remain incomplete. v3 is unreleased and branch-isolated; no rollout or deployment occurs.

## Evidence closure

The [implementation evidence](TASK-194-implementation-evidence.md) and machine
receipt map all eight doubts to public independent installed scenarios and
normal/ASan/UBSan/TSan owner execution. The outer public-lock alternative was
refuted by a reentrant captured-state destructor; policy leases now use the
existing registry writer transaction, with application retirement outside locks.
The existing TSan request-lifecycle allocation-count-only exclusion retains its
TASK-186/188 boundary (root-confirmed); normal/ASan allocation checks and all
independent functional/race assertions execute. No new diagnostic suppression or
blanket gate exclusion was introduced.
