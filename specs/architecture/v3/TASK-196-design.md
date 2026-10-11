# TASK-196: native UDP and basic HTTP/3 ownership

One configured listener owns one transport and reports one actual port by its configuration index. `listener_transport::tcp` remains the aggregate default; appended `udp` explicitly selects the H3 endpoint. A TCP and UDP entry may share a fixed numeric port. Ephemeral entries allocate independently. H3 without UDP, UDP without H3/TLS, invalid enum values and incompatible credentials fail before binding. Duplicate address/port/transport entries fail regardless of their TLS bit. Binding is transactional: activate every endpoint only after all binds succeed.

The alternatives were automatic dual binding from a global H3 bit (ambiguous resource/port reporting and implicit sockets) and a separate public QUIC factory (duplicated routes/credentials/lifetime). Explicit transport retains the existing server and route API while exposing the actual socket choice. Public mixed and rollback witnesses discriminate these choices.

`quic_udp_listener` owns UDP receive, admission, CID dispatch, one shared atomic writer and timer scheduling. `quic_network_connection` owns serialized TLS/key/identity, recovery, flow, H3 framing and close state. These compose mature private engines. Extraction excludes fixture file serving, capture/key observers, manual executors, environment profiles and test constants; production has no test-support dependency.

Each native H3 request has an independent `http2_dispatch` stream affinity and route executor. The synchronized semantic rings carry admitted body/response bytes; a mutex-protected single owned response command carries copied fields. The wire domain applies commands and exact consumed counters. Handler suspension, running frames and cancellation never own the QUIC wire domain. Native RFC9220 returns typed `not_supported`; full parity belongs to TASK-197. Existing serialized private H3 remains the default for protocol fixtures.

A single serial wire/application loop was rejected by the held-running-handler and held-upload sibling discriminators. General worker callbacks mutating protocol state under a shared lock were rejected because arbitrary application work would retain the protocol lock. Native stream ownership follows TASK-195 instead of introducing a cross-protocol runtime.

Admission uses Retry before connection allocation, pending64/32768byte limits, reply16 and a bounded128-route CID/retired history. Each listener prepays131072bytes for admission/dispatch/I/O storage and one timer. Connection storage prepays separate data and critical393216byte envelopes (smaller public limits reduce them), one connection seat and one timer. Recovery bounds64 packet/information records with16 reserved critical records; decode turns cap128 authenticated frames. Receive while a preparation is outstanding caps16 datagrams/32768bytes and charges retained bytes to the connection. H3 caps64 transport records,16 active requests,128 header fields,8192 compressed/16384 expanded header bytes,4096byte DATA chunks,16 receipt and32 output records. Child budgets preserve server aggregate capacities and independent critical pools.

Preparation burns packet numbers and owns immutable bytes/debit. Only actual atomic UDP completion commits sent offsets/recovery/pacing. Proven-unsent returns the debit; ambiguous completion conservatively consumes it once. CID dispatch stores nonowning sinks, avoiding an owner/registration cycle. Alias routes, cached protected close output and keys survive the real three-PTO endpoint and pending completion. Stream frames and their scopes survive until affinity retirement; request drain units are distinct from protocol linger.

`request_stop()` claims terminal state and wakes owners immediately. `stop()` keeps backend, workers, routes/hooks/policy and credentials alive until protocol expiry and completion. Managed mode drives existing backend work. External mode first disables application dispatch, waits for any dispatch claim to finish and exclusively drives bounded native poll/timer turns using real monotonic time. The public driver stays inactive thereafter. Immediate backend destruction and a detached global reaper were rejected: the former frees callback services early, the latter adds an unrelated runtime with user-reference hazards.

TLS binding is transport-aware. Public certificate stores accept H3; UDP requires an enabled default H3 selection. TCP and QUIC filter their own protocols while sharing immutable material. Initial installation and live replacement use the same compatibility function. QUIC uses TLS1.3, QUICv1 and disabled0RTT. PSK stays HTTP/1-only; ACME is never selected for QUIC. Actual listener port is propagated to credential/provider context. Comprehensive rotation, saturation and staged drain remain TASK-197; benchmarks TASK-198/199 and separately retained timer teardown TASK-200.

The causal journal and implementation evidence retain RED/GREEN commands, setup failures and all local proof obligations. Nonlocal BSD/macOS/Windows belong to CI and the v3 PR under the repository policy.

## Named discriminators and provider retirement

The behavioral journal is retained under `.debug/task196/journal.md`; raw commands and receipts are under `build/task196/`. The following tests distinguish the selected boundaries:

| Decision | Refuting failure | Successful discriminator |
|---|---|---|
| Explicit UDP endpoint and transactional activation | Installed malformed mapping, duplicate and partial bind predicates | `public-h3-normal-candidate1/receipt.json`: actual mixed TCP/UDP same fixed port, configuration-indexed ports and rollback in all four package cells |
| Independent application stream ownership | Held-running handler and suspended upload | Both pinned clients' basic scenarios in the public normal/ASan/TSan receipts; sibling route completes before release |
| Owned field handoff | Development corrupted header from copied field view cache | `http3_native_stream` cached-field lifetime witness and installed ordered trailers |
| Real atomic send settlement | Prepared packet has not been sent | `quic_native_owner` fake-backend settlement checks and native-stream unsent write deadline |
| Nonowning CID sinks and real close expiry | `drain-cycle-red3.log` retains owner through a CID registration cycle | Seven owner tests/58 checks retain aliases through pending completion and three PTOs, then release |
| Exclusive external retirement | `retirement-red1.log`: real 10ms timer never executes without a host | Existing external adapter test proves private real-time retirement, live application dispatch exclusion and inactive public driver |
| One asynchronous admission per pending identity | `public-normal4/shared-external/quic-go-basic-process.json`: two admission hooks for one completed TLS connection | One-shot admission claim test plus full client replay; retransmitted Initial retains byte credit |

A required preservation gate exposed a separate provider worker retirement gap. Detached worker `live==0` precedes thread-local destruction; the deterministic C++ destructor witness fails `drain(!early)` on the original lane. A future-only alternative passes that witness but fails a warmed POSIX TLS-key destructor witness: promise readiness can precede OpenSSL-style native thread-local cleanup. An unconditional native join would violate a deadline-bound drain when that destructor is held. Root selected the planner addendum's provider-only lane observer after those counterexamples. One observer is precreated before any provider worker; it owns and joins at most64 handles, releases them and publishes native-provider-retired in plain synchronized state. It captures no lane/provider/application values. Startup failure seals registration and cancels already-started workers without joining the constructing caller. Application lookup workers remain detached and independent. stop/destruction never join; drain waits only until the original monotonic deadline and can be retried after a held native destructor releases. The maximum runtime envelope adds one observer to the existing64 provider plus64 lookup workers, with one fixed handle vector. Unexpected join failure fails retirement; detach is the nonterminating fallback, with bounded handle preservation if the OS also refuses detach. A successful drain proves provider workers' actual native exit, not the observer's short plain C++ publication tail. Current-prefix normal/ASan/TSan preservation and384 fresh immediate-exit processes pass without sleeps after success or new suppressions. This finding is separate from TASK-200's retained timer/manual-executor condition-variable teardown race.


The native H2 preservation gate also exposed a send-completion ordering defect:
the peer can ACK the staged-drain PING before the native writer resumes and
records its successful send. The connection retains at most one exact matching
ACK for the active offered PING. It publishes barrier acknowledgment only after
successful complete output advancement, clears pending ACK on partial advancement
or terminal failure, and retains the existing premature-partial-ACK rejection.
The deterministic full/remainder offer ordering RED and failed-offer discriminator
pin this repair without adding a transport runtime.

TASK-197 owns comprehensive migration accounting. In
`quic_network_connection::credit_datagram`, received bytes currently credit the
original `facts_.budget`; after application path validation, the application path
can instead use `identity_->application_budget()`. The original/shared path uses
the same budget, and the newly validated path is not amplification restricted.
This observation concerns migrated-path receive accounting and credit/budget
recovery; TASK-196 does not claim comprehensive migration coverage or an
unvalidated-path amplification proof from the basic public matrix. TASK-197 must
verify the original/shared versus newly validated path distinction under its
existing migrated-path policy/metadata and recovery action items.


Authenticated terminal close precedes path activation and application effects.
After successful packet authentication to this connection, an unvalidated
candidate path can carry CONNECTION_CLOSE without becoming the application path.
Co-packed STREAM cannot execute, send/amplification restrictions remain, terminal
notifications occur once and real expiry owns final retirement. Forged ciphertext
cannot close the connection. Production and the affected capture fixture share
this ordering; independent protected-client RED/GREEN tests and current native,
public and capture sanitizer proofs cover it.

Capture observation retains every live challenge/response predicate. An unanswered
challenge retires only when an independently authenticated matching zero-error
client close precedes its arrival, with the same close packet identified at both
boundaries and no later server STREAM. No missing/unmatched/undecoded close or
challenge-before-close can receive this treatment. The original failure remains
retained and fails the unchanged TASK191 server closure oracle.

The task-local pinned ns3.46.1 simulator derivative changes only the elapsed-time
clock and period in WallClockSynchronizer to steady_clock. Origin and now retain
one nanosecond domain; relative waits, original limits, loss/rebind scenarios and
all scenario binaries remain unchanged. Exact upstream archive, minimal patch,
compiler/build and original/derived image/binary identities are recorded in the
clock receipt. A real original-image pacing RED and120-event patched-image proof
run under observed host realtime backsteps. This compatibility affects only the
explicitly selected simulator; host, application, client and protocol clocks are
unchanged. Full proof verifies its provenance and the unchanged nine-stage gate
inventory, including exact compiled-input scope when reusing completed checks.
