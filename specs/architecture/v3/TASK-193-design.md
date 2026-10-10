# TASK-193 public credential and accepted-connection design

Dominant caller: an installed C++20 application supplies PEM certificate/key values, associates a credential store with TCP listeners, registers ordinary routes and chooses managed or external readiness. Provider and OS objects, generations and scheduling stay private. Existing three-field plaintext aggregate initialization and header-only option validation remain valid.

Three divergent interfaces were compared sequentially in the executor's serialized ownership scope:

| Alternative | Depth | Locality | Seam |
| --- | --- | --- | --- |
| Opaque shared credential store, value configuration, `replace(config)`; optional listener association | Hides validation, context construction, publication, exact SNI/default and retention behind one mutation | Publication stays in the existing registry; listener only acquires a snapshot | Fits reusable ownership and immutable generations |
| Server-owned `install_credentials(listener_index, config)` | Deep single-server common path, fewer classes | Couples publication to server lifetime and numbering | Sharing across servers needs another surface |
| Public TLS session factories/callbacks | Wide shallow surface exposes handshake scheduling and retention | Applications repeat retirement rules | Provider/runtime boundary leaks into installed callers |

Choose the opaque store. Public `server::tls_credentials` has shared opaque implementation, `tls_credentials_config` and certificate-only `tls_host_credentials` value inputs. `listener_options::credentials` is an optional shared store. Inputs are used synchronously and not retained as PEM; successful replacements publish fully validated immutable registry generations, failed replacements preserve the active generation. The listener retains the store and each accepted session pins one generation before session creation. Exact canonical SNI selects within that generation; unknown/absent SNI uses its configured default. Independent stores never share keys.

Keep one native connection lifecycle: the listener reserves a connection seat and a shared retirement lease enters the drain scope before the existing connection engine submits an explicit handshake using its existing `io_connection_owner`. Only successful supported negotiation starts H1 reader/writer/router/watchdog tasks. The engine retains its TLS socket view through finalization, with the TLS pump's existing child retirement rules. This avoids an independent handshake record, reservation transfer and second native connection owner. Certificate provider steps use the existing bounded handshake runtime via a private listener binding; no public executor callback or new dependency.

Validation precedence is pure structural options first (identical in both loops), unavailable build/protocol next, then missing/unpublished credentials and listener policy compatibility. TLS-off credential replacement returns `not_supported`; native TLS requests remain typed/unbound. TLS-on accepts certificate-only H1 initially; mTLS/PSK public profiles and ACME remain TASK-194 responsibilities. All listeners bind before any accept task starts; a failed later bind synchronously releases earlier binds and clears reported ports.

Decision to refute: each accepted connection retains a validated immutable generation and one native owner through handshake, H1 work and retirement.

| Doubt | Required discriminator | State |
| --- | --- | --- |
| Public caller needs provider/detail macros or source headers | Same-source staged TLS-off/on declarations, compile deps and dependency graphs | Cleared locally; see implementation evidence |
| Socket binding masquerades as TLS capability | Independent verified H1.0/H1.1, shared/static and managed/external | Cleared locally; see implementation evidence |
| Replacement or caller destruction invalidates pending session | Held handshake/session, invalid/valid replacement, destruction, ASan/TSan | Cleared locally; see implementation evidence |
| SNI or listener policy crosses store boundaries | Two stores/listeners, exact/unknown/absent SNI, pre-bind validation | Cleared locally; see implementation evidence |
| Handshakes bypass connection/storage bounds | Saturated stalled peers, healthy/control requests and retirement counts | Cleared locally; see implementation evidence |
| Stop/drain loses handshake or invokes callbacks under I/O lock | Deadline, disconnect, handler-safe stop/drain and late completion tests | Cleared locally; see implementation evidence |
| Partial binding leaves admission live | Occupied later port, reusable earlier port and zero reported bindings | Cleared locally; see implementation evidence |
| Metadata or protocol policy is fabricated | Real peer and immutable TLS observations, disabled H1 rejection | Cleared locally; see implementation evidence |

Exact receipts and final dispositions belong in TASK-193-implementation-evidence.md. No public/concurrency commit with open doubts. Reversible task isolation and pre-bind validation cover rollback; no rollout flag is needed for unreleased local v3. Nonlocal platform tests are CI/v3-PR owned.
