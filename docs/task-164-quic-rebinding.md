# TASK-164: bounded QUIC identity and validated rebinding

The native private `quic_connection_identity` coordinates CID routes, peer
CID selection, validation controls, amplification budgets and path activation.
Keys, packet numbers, reliable information, congestion scheduling, flow offsets
and HTTP/3 exchanges remain in their existing components. The TLS test network
owner composes this production state machine with real protected packets and
HTTP/3. This task does not enable the public HTTP/3 listener.

## Bounds and ownership

The advertised active CID limit is four. Local issuance respects the negotiated
peer limit (default two, clamped to four). Each direction has 32 cumulative CID
records, including sequence zero; exhausted history refuses further issuance
rather than forgetting IDs or reset tokens. At most eight reliable peer
retirements may await completion. Sparse sequence numbers consume one record,
not an array indexed by their value. All CID/token views are copied.

Additional local IDs register in the existing listener demux before retention
of their advertisement. RNG, collision, listener or recovery-capacity failure
rolls back unpublished routes. Random collision searches stop after four
attempts. ACKs complete advertisements without retiring routes. RETIRE removes
a route; the enclosing destination CID and unissued sequences cannot retire.
A valid retirement attempts replacement issuance within the bounds. Retired
CID/token history remains owned so reuse is rejected. NEW_CONNECTION_ID and
RETIRE_CONNECTION_ID use the existing reliable information scheduler, including
fresh packet numbers on loss and exactly one completion on late ACKs.

There are three path records: the active path and up to two other records.
Candidates and previous active paths expire within three validation intervals,
capped at fifteen seconds. Active and pending-send records cannot expire.
Challenges have at most three actual emissions. The timer interval is at least
one second and the current application PTO, including backoff. The previous
path gets a challenge on apparent migration; newer non-probing traffic can
supersede a candidate or restore a previously validated path.

The critical storage lease charges the state plus conservative allowances of
2048 bytes per path budget and 256 bytes per CID route. Listener lookup and its
retired history retain their independent bounds. Alias sinks hold weak owner
references. Releasing the original registration and fixture references destroys
the connection, removes aliases, and releases its storage envelope.

## Authorization, validation and input

Candidate records follow authentication, freshness inspection and complete
frame decoding. Unknown, failed-authentication, duplicate and stale packets do
not select application output. A full candidate set drops the candidate while
preserving the active connection. Per-datagram credit is counted once across
coalesced packets; admitted Initial credit is not counted twice.

Each candidate owns an independent amplification budget keyed by canonical
peer, local endpoint, scope, interface and socket. Controls reserve that budget
and pin the copied destination CID until actual completion. Proven-unsent
completion burns the packet number and refunds the reservation. Successful or
ambiguous emission keeps the debit. A submitted application emission likewise
pins identity; the owner drops arrivals while that completion is pending and
never acknowledges their dropped payload.

PATH_RESPONSE echoes its challenge on the receive path and is one-shot after
actual emission. Validation controls pad to 1200 bytes when credit permits,
using the selected destination CID's width. An unpadded challenge can establish
reachability, but activation still waits for a subsequent padded validation.
Random challenge tokens bind only after actual emission. An expired, wrong or
proven-unsent token cannot validate. A matching response arriving on any path
validates the path on which the challenge was sent, not its arrival path.

Activation requires the newest fresh non-probing application packet's selected
path, reachability, minimum-MTU validation and live policy acceptance. PATH-only
traffic cannot grant migration intent. Discovery and activation each classify
against the actual runtime `server::peer_policy`; port-only rebinding also
reevaluates policy. Denied paths dispatch no candidate-origin handlers and get
zero application bytes. STREAM/RESET/STOP_SENDING/flow/ACK processing and packet
receipt are withheld until authorization and validation. Retransmission with a
fresh PN supplies withheld data; dropped STREAM packets never enter ACK ranges.
Pure probing packets may be committed normally.

Different local endpoints prefer unused peer CIDs. The unchanged incoming CID
and unchanged local endpoint/socket/interface permit NAT rebinding's reuse
exception. Retiring a candidate's peer CID discards that candidate and its old
validation token rather than reusing the active CID on another local endpoint.

These choices follow [RFC 9000 §§5.1 and 19.15–19.16](https://www.rfc-editor.org/rfc/rfc9000.html#section-5.1),
[§8.2](https://www.rfc-editor.org/rfc/rfc9000.html#section-8.2), and
[§9.3–9.5](https://www.rfc-editor.org/rfc/rfc9000.html#section-9.3).
General PMTU discovery, draining, reset packet generation and preferred-address
migration remain later tasks.

## Recovery and request peers

Activation first requires completion or explicit abandonment of any preparation.
It preserves packet-number spaces, retained information, stream state, H3 state
and flow offsets. It resets congestion, pacing and RTT, conservatively including
port-only changes. Outstanding previous-path flight becomes pending information;
late ACKs still complete that information without sampling the new RTT or
changing the new window. Off-path probes carry a distinct recovery epoch and
cannot sample or reduce active-path estimates.

The H3 engine's private peer setter changes the peer for future complete
semantic request heads. An exchange takes its by-value snapshot when that head
is constructed, after all initial HEADERS arrive. Stream opening and partial
HEADERS do not snapshot. Suspended handlers, bodies and responses retain their
existing peer/port after rebinding. Forwarded headers do not alter transport
identity; ordinary request-time policy remains the shared dispatch pipeline's
responsibility.

## Verification and doubt closure

Local builds use Clang 18 with `-std=c++20`. TLS-off core/state tests link no
OpenSSL. TLS-on tests use the task-local static OpenSSL 3.5.9 provider whose
archive SHA256 is
`603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`.

Recorded RED/GREEN receipts include initial missing-driver behavior, CID
reliable retention, H3 snapshots, protected-owner path rejection, retirement
completion slots, full-packet retirement ordering, CID-width padding, PTO
backoff, pending-send pinning, inactive-path reclamation, retained peer history
retired candidate CID invalidation and off-path persistent-congestion isolation. Receipt names are
`task164-*-red*.log`, `task164-*-green*.log` and `task164-final-*.log`.

| Named doubt | Cleared evidence |
| --- | --- |
| Spoofed, replayed or denied paths redirect output or dispatch handlers | Protected `http3_network_owner` negative cases; withheld PN exclusion from every decoded application ACK range; two parked streams survive validated rebinding |
| A cross-arrival response activates its arrival path or an unsent challenge | Identity sent/unsent and cross-arrival token cases; token ownership and expiry |
| Reordered or probing-only traffic chooses a stale path or strands streams | Highest non-probing supersession, old-path challenge and bounded candidate timeout/reclamation cases; retained owner responses/body/FINs |
| CID issuance/retirement leaks or grows without bound | Active/cumulative/pending bounds, sparse/max sequence, retained CID/token conflicts, rollback and alias owner destruction cases |
| A preparation uses a retired CID or transfers/refunds actual-send credit | Busy identity under application preparation, separate pinned budget, sent/ambiguous/proven-unsent receipts and retired candidate invalidation cases |
| Old flight changes new RTT/window or damages information accounting | Recovery migration with outstanding STREAM/FIN, replacement and late ACK; off-path probe ACK/loss and persistent-congestion interval cases |
| Runtime policy mutation or rebinding changes an existing request peer | Discovery/activation policy mutation cases and suspended A/new B/partial-header snapshot cases in both H3 engine and protected owner |
| Padding/probe loss causes reflection or duplicates reliable scheduling | Independent 3x credit, CID-width 1200 padding, unpadded then padded validation, PTO spacing and one-shot response tests; existing scheduler handles path emissions |

Focused builds and tests are run from `build/off` and `build/on`:

```sh
make -C src -j4 libhttpserver_v3core.la
make -C test -j4 quic_connection_identity quic_repacketize http3_exchange
(cd test && ./quic_connection_identity && ./quic_repacketize && ./http3_exchange)
# TLS-on owner and supported-provider tests:
make -C test -j4 http3_network_owner quic_server_admission quic_recovery_tls quic_tls_handshake quic_interop_tls
```

Final receipts include impacted dispatch, amplification, recovery, flow recovery,
congestion, pacing, send scheduling, H3 streaming and cancellation suites.
All seventeen focused suites passed: 166 tests and 8656 checks. Identity,
protected-owner and repacketization suites also passed ASan/UBSan/LeakSanitizer.
Changed-file cpplint and `git diff --check` gate the implementation. Task-local
ASan/UBSan probes instrument the changed core modules and identity/owner tests;
LeakSanitizer runs outside the sandbox ptrace layer. They link the remaining
verified native core/provider archives. Nonlocal BSD/Windows checks are
CI/v3-PR owned under AGENTS.md and were not executed locally.

`make -j6 check-local` passed its linkage, header, README, release-note,
readiness, install-layout and packaging-hygiene gates. Its Doxygen gate reported
SKIP because Doxygen is absent and the repository assigns that gate to CI.
External aioquic/quic-go smoke clients are absent from the retained local tools;
the native fixture/interop endpoints were compiled and protected-owner tests
executed. The broader external runner matrix remains TASK-173-owned.
