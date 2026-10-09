# TASK-166: QUIC lifecycle and disposal

The private `quic_connection_lifecycle` arbitrates active, closing, draining and
closed states. Its calls are serialized by the connection owner, time is explicit,
and the component neither owns keys nor invokes callbacks. Reasons are copied
into 256 bytes of owned storage. The first terminal cause commits state before
application cancellation; competing causes cannot replace it. Entering draining
from closing preserves the original retention endpoint.

## Timers and ownership

The idle policy uses the smaller nonzero transport parameter, or the sole nonzero
parameter. Two zero values disable idle expiry. An accepted authenticated receive
restarts idle activity; only the first possibly emitted ack-eliciting packet after
that receive can restart it again. Preparation, proven-unsent completion and
ACK-only output do not restart it. The deadline applies a three-current-PTO floor.
Recovery exposes the RTT-based PTO without the path-validation one-second floor or
probe backoff. Backoff still doubles loss-recovery probe timing, but cannot keep
moving idle expiry on an unresponsive path; consecutive PTOs remain limited by
idle timeout ([RFC 9002 §6.2.1](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.1)).
Arithmetic saturates at the representable duration/time limit.

Idle expiry is silent and closes immediately. CONNECTION_CLOSE and received
stateless reset retain connection identification for three PTOs. Closing responses
are capped at three possibly emitted datagrams, separated by a PTO; response wakes
are distinct from the retention endpoint. A submitted close or amplification-blocked
response exposes only retention timing, avoiding immediate retry spins while it
waits for completion or additional received credit. Draining has no output, ordinary recovery
or path-validation timers. Closed state has no deadline. Stale timer calls are
idempotent.

`quic_connection_identity::stop()` freezes CID issuance, path migration/validation
and ordinary identity control work. It retains existing local aliases and selected
path facts. `detach_routes()` retires every remaining alias and reset route at the
endpoint, independently of object destruction. Delivery generations suppress
already queued packets after removal, and stale removal handles cannot remove a
replacement registration. Previously retired CIDs are never restored.

A caller must retain the connection owner through the endpoint and each outstanding
UDP completion. Weak alias forwarders alone do not retain it. The protected fixture
keeps an explicit shared owner reference and exercises both initial and additional
aliases immediately before expiry. Public listener ownership remains a later task.

## Terminal output and cancellation

The protected HTTP/3 fixture composes the production lifecycle, identity, recovery,
key and request-engine components. It intercepts CONNECTION_CLOSE before applying
any application or identity frames from the packet. Normal TLS progression, stream
delivery, retained output and recovery completion forwarding stop immediately.
Protocol closure does not populate the fixture's diagnostic failure string.

The owner performs one cancellation fan-out through
`http3_request_engine::disconnect()`, freezes identity work, and stops TLS provider
work. Pending packet/debit storage stays pinned until the actual completion.
Possibly emitted and ambiguous sends consume amplification debit; only proven-unsent
sends release it. Late completions retire ownership without forwarding new
application effects. Duplicate completions after termination are harmless.
Destruction conservatively treats an unresolved preparation as ambiguous.

Once pending output settles, request engines, application file handles, transport
streams and flow state are released. Handler-initiated closure defers that
retirement until the current receive turn returns, so its borrowed stream references
stay valid; subsequent frames from the packet cannot start another handler. Local close prepares one bounded protected terminal
datagram. It contains only CONNECTION_CLOSE and padding, bypasses ordinary reliable
information/retransmission scheduling, and uses the selected path's amplification
budget. Confirmed connections use application protection; unconfirmed connections
use available Initial/Handshake protection. Early application close converts to
transport APPLICATION_ERROR (`0x0c`) and clears its reason. Initial terminal output
uses the conservative 1200-byte floor. Missing keys, failed protection or insufficient
amplification capacity do not emit ordinary traffic.

Closing retains the selected protection-level packet keys for authenticating a
peer close until draining or expiry; unused early application keys are discarded. TLS secret installation cannot resume after terminal
claim. Draining/closed clears all remaining packet-key generations; repeated clear,
expiry and destruction do not release key storage twice. Server Initial keys retire
after accepted Handshake processing and Handshake keys retire on confirmation.
Confirmation publishes the updated recovery environment immediately, so a close
before the next timer turn uses the confirmed PTO and peer ACK delay.

The fixture's separate watchdog detects test hangs. It does not implement negotiated
idle timing. HTTP/3 GOAWAY/graceful application drain and public H3 listener enablement
remain their own tasks.

## Stateless reset

Peer reset tokens are copied with their CID records. Advertisement alone does not
make a token eligible: the owner must report successful or ambiguous emission using
that live peer CID on the remote endpoint. Each CID has at most three remote endpoint
associations, within the existing 32-record CID history. Proven-unsent preparation,
wrong remote endpoints, near-match tokens and retired IDs cannot authorize reset.
Retirement removes associations and their dispatch registrations; final detachment
also removes eligibility in identities without listener registrations.

Both identity and listener compare fixed 16-byte tokens with `constant_time_equal`,
scanning their bounded registries without token-dependent early exits or secret hash
keys. Tokens come from the end of a whole datagram of at least 21 bytes. Listener
fallback handles unrelated or unparseable destination CIDs, using the existing
owner port, queue limits and retirement generation. Known-CID packets reach the
owner, which detects reset only after the first packet cannot authenticate.
Duplicate token/endpoint registrations cannot redirect another owner. If an
actual emission cannot register reset eligibility because capacity is exhausted,
the owner retires that completion/debit before claiming a local INTERNAL_ERROR
close; it never retries the already completed send. Listener reset capacity is independently bounded by its configured registry capacity.

Unknown datagrams stay silent. Autonomous stateless-reset generation after loss of
connection state is optional and is not implemented: no persistent reset-key service
or crash-recovery token source is introduced.

## Local evidence

Fresh out-of-tree Clang C++20 builds use `build/off` and `build/on`. TLS-off disables
provider discovery. TLS-on uses the genuine OpenSSL 3.5.9 provider already retained
at the repository's `build/tooling/provider` prefix; system OpenSSL is not substituted.

Task-local RED/GREEN logs under `/tmp/task166-*` cover idle/terminal arbitration,
reset eligibility/routing, response wake timing, cancellation, observable key
retirement and conservative unresolved-send accounting. The focused suites and
fixture compilation results are recorded in the implementation receipt. TLS-off
focused suites passed 1,493 checks across nine executables. TLS-on passed 7,254
checks across seven executables covering key state, protected network lifecycle,
admission, handshake, recovery/TLS, TLS session and interop provider tests. Both
modes passed `make -j6 check-local`, including native
linkage, header and installed-consumer hygiene. Doxygen was unavailable and its
existing gate explicitly skipped to CI. Shared UDP and interop endpoints compiled.

Changed-file cpplint and `git diff --check` gate this task. New production functions
stay at CCN 10 or below; pre-existing identity functions at CCN 13–50 remain, with
modified function complexity unchanged or reduced. The repository-wide file-size
probe still reports the unchanged `io_poll_backend.cpp` at 509 SLOC against 500;
changed production files fit the existing ceiling. Those baseline findings are
outside TASK-166 and are reported rather than suppressed.

BSD, Windows and other nonlocal platform checks were not executed locally. CI and
the v3 PR own those checks under the project workflow policy.
