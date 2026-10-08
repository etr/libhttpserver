# TASK-158: QUIC congestion, pacing, and send scheduling

Implemented in the existing `task/TASK-158` worktree from `v3` at
`75471883a917e69e184ac035e2ffff36f0628bf6`. The task remains **In Progress**
for caller-owned validation and finalization. No commit, merge, or worktree
cleanup was performed by the executor.

## Owner and transport contract

`quic_recovery` owns the sent ledger and authoritative wire bytes in flight.
NewReno and pacing are allocation-free values in its private implementation;
all three packet-number spaces share their state. Emission time must be
nondecreasing across spaces. Invalid and duplicate ACKs cannot repeat growth,
and late ACKs of lost packets do not restore flight accounting or grow the
window. Discarding keys removes flight bytes without treating them as loss.
Peer ECN counters are not used as validated congestion feedback.

The future serialized connection owner must use `prepare_scheduled_packet`,
provide actual packet protection/padding overhead, protect the reserved packet
number, then call `check_scheduled_emission` immediately before emission with
its actual wire size and flags. With no intervening owner event, a successful
check admits that emission; `commit_sent` records it without allocation. Its
recheck catches violations of that owner contract. Flow final sizes, RESET
state, key availability, send permission, congestion and pacing are checked
before emission. A failed transport send must abandon the preparation.

The original reserve/prepare interfaces remain low-level seams for recovery
and codec fixtures. They do not promise scheduled admission. Every committed
in-flight packet still contributes to congestion and pacing observations.
Preparation, encryption failure, invalid commit, and abandonment consume no
pacing tokens, flow credit, probe grant, or stream service turn.

The result distinguishes `congestion_blocked`, `pacing_blocked` (with a
deadline), and `unavailable` from storage capacity, invalid input, no data,
and insufficient encoding space. `next_send_deadline` reports the most recent
pacing admission deadline; ACK/environment/discard events invalidate it and
must prompt another admission attempt. Delayed emission charges its actual
time, not preparation time. Backend timer ownership remains outside this core.

## Configuration and bounded resources

- Maximum datagram size defaults to 1200 wire bytes; supported configurations
  are 1200 through 65535. The initial window is
  `min(10*M, max(14720, 2*M))`; the minimum window is `2*M`.
- Slow start adds newly acknowledged eligible wire bytes. Avoidance accumulates
  byte credit and increases by one datagram per acknowledged window, avoiding
  integer multiplication overflow. A loss epoch halves once. Persistent loss
  reduces to the minimum and preserves the slow-start threshold.
- Application/flow-limited scheduled emission suppresses growth when no eligible
  stream work remains below the window, or when the caller explicitly marks
  the request limited. Pacing blockage alone does not mark packets limited.
- Pacing uses a conservative rate of one window per smoothed RTT, a 1 ms RTT
  floor, and a two-datagram burst cap. Idle time cannot accumulate more credit.
  Non-flight ACK-only work bypasses it; padded ACK packets still charge it.
- Ordinary streams leave `min(control_reserve, window/2)` unused; the default
  reserve is 1200 bytes. Thus one ordinary datagram can progress at the minimum
  window. CRYPTO, reliable MAX/RESET, and ACK work retain protected storage and
  descriptor admission and may use the full shared window. Reserve never
  authorizes ordinary control to exceed the window.
- PTO expiry replaces a space's outstanding grant with at most the reported
  one/two probes. Only successful scheduled eliciting emission consumes it.
  Probes bypass window/pacing admission, remain in flight and charge the pacer,
  and still require keys/send permission. ACK progress retires unused grants;
  discard retires grants for that space. Probe expiry is not a loss event.

Stream scheduling uses deficit round robin with a datagram-sized payload
quantum and at most one quantum of credit per stream. Retained segments of
one stream share one entry; sparse stream IDs buy no extra turns. Small
segments continue the same turn until its credit is consumed. Empty FIN
costs one service unit. Blocked/cancelled/completed streams are skipped;
retransmissions preserve information IDs and alias completion behavior.

At most two emitted Application controls or ACK-only packets precede eligible
ordinary service. When storage, the ordinary window, or pacing blocks data,
protected ACK/control progress continues. ACKs can accompany the selected data
turn; insufficient frame output falls back to ACK-only preparation. With N
continuously eligible streams, at most `(N-1)*M` ordinary service units
precede a stream's next turn, with at most two control emissions between each
data emission. Selection is bounded by the configured retained descriptor
count. No HTTP/3 stream criticality is inferred from stream IDs.

`storage_capacity()` charges one scheduling entry per possible information
record plus `3*max_sent_packets+1` congestion outcome summaries. Constructor
allocations are staged; failed admission rolls back them and the reservation.
Successful commit never grows these vectors. Outcome summaries carry no
flight-byte/retransmission ledger. They preserve sampled lost endpoints and
ACK separators across packet collection and spaces. Persistent congestion
uses the strict duration boundary
`3*(smoothed_rtt + max(4*rttvar, 1ms) + peer_max_ack_delay)`, including ACK
delay for every space, following [RFC 9002 section 7.6](https://www.rfc-editor.org/rfc/rfc9002.html#section-7.6).

Adjacent resolved outcomes are compacted. A late ACK into a compressed lost
interval invalidates the whole interval conservatively. If summary capacity
is exhausted, only the oldest prefix is forgotten; this can delay detection
of a historical run but cannot create a false persistent-loss interval.
Repeated expiry without a new loss cannot collapse the window again.

## Local implementation evidence

No sleeps are used in QUIC tests. The new model suites contain 3 congestion
and 2 pacing tests; send scheduling contains 19 tests. The storage regression
also proves allocation-free scheduled commit and complete budget release.

RED observations and subsequent GREEN runs are retained under ignored
`build/task158-off/`: missing model headers and scheduling API; missing
persistent-loss collapse; application/flow limitation and padded ACK pacing;
persistent-loss slow start; emission flow/time recheck; ACK-after-collection
separation; probe retirement and final wire-size deadline; continuously
replenished ACK fairness. Existing recovery/flow/codec suites ran after the
corresponding changes. Logs retain both failures and successful repairs.

Final configurations and checks:

```sh
bash bootstrap
# In build/task158-off:
../../configure --disable-v3-tls --disable-examples \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -C src -j2 libhttpserver_v3core.la
make -C test -j2 quic_congestion quic_pacing quic_send_schedule quic_ack_state \
  quic_recovery quic_repacketize quic_flow_control quic_flow_recovery \
  quic_flow_storage quic_stream_state quic_reassembly quic_frame quic_transport_parameters
# make -C test check uses these same names for check_PROGRAMS and TESTS.
make check-v3-native-linkage
make check-local
```

The sanitizer configuration uses `-std=c++20 -O1 -g
-fsanitize=address,undefined -fno-omit-frame-pointer` for the core and seven
focused suites: congestion, pacing, scheduling, recovery, repacketize,
flow-recovery, and flow-storage. The TLS-on configuration reuses the existing,
unmodified provider at `/private/tmp/task129-provider/install`, configured
with `--enable-v3-tls`, its include directory and `-lssl -lcrypto`. Eight suites
cover congestion, pacing, scheduling, recovery-TLS, TLS session/handshake,
flow-storage, and flow-recovery. Each build has final `completed-*.log` receipts: TLS-off 13/13 programs,
TLS-on 8/8, and ASan/UBSan 7/7. TLS-off/on native linkage and TLS-off
`check-local` pass. An additional linkage audit of the instrumented build
rejects the injected ASan runtime (`libclang_rt.asan_osx_dynamic.dylib`) under
the existing third-party allowlist; no allowlist change was made. The ordinary
TLS-off/on builds supply the native linkage receipts.

An overlapping TLS-off core archive link and staged-install build caused
transient missing-symbol errors. The generated task-local archive was rebuilt
and its complete build/test/linkage/check-local chain rerun sequentially; the
final receipts are green. The earlier diagnostics remain in ignored
`off-link-race.log` / `off-install-link-race.log`.

Changed-file cpplint, CCN <= 10, warning-suppression checks, and diff whitespace
checks pass. Repository-wide complexity has 17 existing violations, and file
size has the existing `io_poll_backend.cpp` 509-SLOC violation. Their complete
outputs compare byte-for-byte with the untouched `v3` checkout; no threshold
was raised and no unrelated source was changed.

Coverage maps NewReno/loss/persistent-loss/probe admission to congestion
acceptance; stream fairness/flow skip to PRD-V3N-REQ-008; protected control
progress and charged bounded storage to PRD-V3N-REQ-027 / DR-V3-006; provider-free
private ownership to DR-V3-001. BSD, Windows and other nonlocal checks remain
assigned to CI/the v3 PR. UDP/HTTP3 interoperability, amplification enforcement
at the transport, coalesced datagram sizing, PMTU, migration and ECN activation
remain future transport work; no such proof is claimed here.

## Validation repair, iteration 1

The two authorized architecture findings are repaired. Scheduled fairness now
encodes the bounded STREAM candidate into the available frame output and checks
its encoded size plus protection overhead, rather than the request's maximum
wire allowance. The final scheduled emission check still admits the actual
protected/padded packet. An outstanding preparation returns `busy` before the
candidate encoder can alter its output.

If an information packet is rejected by congestion or pacing while an ACK is
pending, preparation abandons that packet and retries protected ACK-only work.
The retained information remains pending, with no completion, flow charge,
probe consumption, or fairness turn from the abandoned preparation. ACK-only
plans still use the normal scheduled checks for keys, send permission and final
wire/padding admission.

Four deterministic scheduling regressions reproduced the original window and
pacing failures before repair (`build/task158-off/repair-iter1-red-tests.log`).
A fifth regression reproduced output corruption from a busy retry while the
new candidate encoder was active, then passed after the early busy check
(`repair-iter1-busy-red-tests.log`). The repaired focused TLS-off scheduling
suite passes 24 tests / 365 checks (`repair-iter1-green-tests.log`). Changed-file
cpplint and production CCN <= 10 pass (`repair-iter1-cpplint.log` and
`repair-iter1-complexity.log`). The coordinator owns the final TLS-off/on,
sanitizer, linkage and check-local gate barrier; these repair receipts alone
do not claim that barrier was rerun.
