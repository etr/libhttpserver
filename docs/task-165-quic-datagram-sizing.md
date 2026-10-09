# TASK-165: QUIC datagram sizing and black-hole recovery

## Units and bounds

All sizing values count UDP payload bytes, including packet headers and
protection. Recovery starts each active path at 1200 bytes. The configured
local ceiling and validated peer `max_udp_payload_size` bound discovery;
neither authorizes an ordinary send above the confirmed path size. The peer
parameter defaults to 65527. Invalid limits leave state unchanged. Install the
peer ceiling after TLS transport-parameter validation and before discovery.

Scheduled sends recheck actual wire size immediately before emission. The
low-level packet reservation/preparation APIs remain accounting fixture seams.
Path validation controls remain standalone and use 1200 bytes on candidate
paths. Successful identity activation, including port-only rebinding, resets
path sizing and discovery; prior-path ACKs cannot confirm the new estimate.

`quic_datagram_sizing` measures actual CID, token, packet-number and length-field
widths and the 16-byte protection tag. It orders at most three packets as
Initial, Handshake, Application, with one packet per space and one destination
CID. Short-header packets end the datagram. Client Initial datagrams and server
datagrams containing eliciting Initial packets reach 1200 bytes by padding
inside the last protected packet. Server ACK-only Initial packets need no such
minimum. Padding is applied to the whole datagram once; padded ACK-only packets
consume flight and pacing capacity. These rules follow
[RFC 9000 sections 12.2, 13.2.7, 14.1 and 14.4](https://www.rfc-editor.org/rfc/rfc9000.html).

## Atomic sends

Recovery owns one bounded datagram transaction. Begin, prepare one packet per
space, protect every packet, and check all actual lengths before submitting UDP.
Every preparation retains its burned packet number, information slice, ACK
snapshot and flow reference. Successful emission commits all records without
allocation. Proven-unsent completion abandons the whole transaction and charges
no flow, pacing, amplification, PTO grant or PMTU attempt. Pending transactions
prevent identity activation and unrelated packet preparation.

The final check enforces the total path allowance, minimum Initial size, write
keys, and aggregate congestion/pacing admission. Any ordinary data packet keeps
the ordinary/control window reserve in force. Only a batch consisting entirely
of in-flight PTO probes can use the PTO exception. The fixture owner reserves
amplification credit once for the complete datagram and retains each packet's
individual flight bytes. It can coalesce later ready handshake packets when a
large Initial occupies the first datagram by itself.

## Discovery and fallback

PMTU probes are standalone Application PING plus PADDING packets. They require
handshake confirmation and a validated active path. One search probe may be
outstanding; the next candidate advances 128 bytes toward the peer/local
intersection. Preparation and unsent completion do not start attempts or timers.
The emitted preparation token identifies the current search generation. Only a
new, validated ACK for that probe, on its active path and before its explicit
PTO-derived deadline, can confirm its actual emitted size. Timeout releases its
flight once, without reducing the congestion window or contributing persistent
congestion evidence. Three failed attempts impose a cooldown of ten current
application PTO intervals. Later owner send opportunities can resume discovery.
Failed discovery alone preserves an already confirmed working size.

Sent records retain the actual datagram size and identity. Delivery of any
constituent marks all surviving records delivered, even after the ACKed record
is collected. Coalesced packets cannot count as separate black-hole failures.
Three unsuccessful PTO recovery opportunities in the affected packet-number
space involving distinct larger
ordinary datagrams reduce ordinary sends to 1200 bytes and enter the same
cooldown. Relevant progress clears suspicion. PTO expiry grants fresh packets
rather than declaring all outstanding packets lost. Retained STREAM/CRYPTO
information is encoded again under the reduced allowance; offsets, FIN and
alias completion remain authoritative. A late ACK of an older alias completes
information at most once.

Sizing updates preserve congestion recovery history, additive growth credit,
and pacing debt; they update the minimum window, burst bound and stream quantum.
Deficit credit is clamped on shrink. A path activation still performs the
existing conservative RTT/congestion reset. Bounded transaction and discovery
metadata are charged by `quic_recovery::storage_capacity`.

## TDD evidence and local checks

The task-local receipts under `/tmp/task165-*.log` recorded these expected RED
failures before their implementation:

- A 1472-byte configured allowance selected unconfirmed oversized STREAM data.
- Probe preparation was unavailable and peer ceiling installation failed.
- Datagram layout and multi-space preparation were unavailable.
- A deterministic larger-datagram drop script retained the larger send size at
  its third unsuccessful recovery opportunity.
- A padded ACK-only scheduled packet could bypass flight accounting.
- Fixed transaction metadata was not charged under a small connection envelope.
- Path controls exceeded the confirmed send size.
- Collecting an ACKed coalesced constituent allowed the same delivered datagram
  to contribute black-hole suspicion.
- An Initial-space PTO incorrectly counted newer Application datagrams before
  their own recovery opportunity.
- The protected owner emitted no larger discovery probe.

The protected coalescing assertion observes the full handshake: an individual
first Initial can legitimately occupy the whole datagram, and ACK-only Initial
or Handshake/Application coalescing need not reach the Initial minimum.
The sizing unit suite separately proves exact 1200-byte padding.

Deterministic scripts use the existing logical clock and bounded network
harness, select drops by actual size, replay twice, compare byte traces and
assert final zero harness resources. No sleeps or randomness enter these
acceptance traces. Existing TLS owner fixtures retain their established clock
and peer-driving conventions.

The build is out of tree, using `bash bootstrap`, Clang C++20, and
`--disable-examples`. TLS-off checks live in `build/task165-off`. TLS-on checks
live in `build/task165-on` and use the retained static OpenSSL provider under
`/home/etr/progs/libhttpserver/build/tooling/provider`; the system provider is
insufficient for the native TLS configure requirement. Build the core library
before test targets, and run TLS executables from their build's `test` directory
so credential fixtures resolve. Tests use Littletest under `test/unit`, with
production sources under `src/detail` and guarded private headers under
`src/httpserver/detail`. Changed C++ files are checked with `cpplint`.

BSD, Windows and other nonlocal platform checks remain assigned to CI and the
v3 PR, as required by repository policy. No nonlocal receipt is claimed here.

Local implementation checks completed successfully:

- TLS-off: `quic_path_mtu`, `quic_datagram_sizing`, `quic_send_schedule`,
  `quic_recovery`, `quic_repacketize`, `quic_connection_identity`,
  `quic_congestion`, `quic_pacing`, `quic_amplification`, `quic_flow_recovery`,
  `quic_flow_storage`, `quic_packet`, `quic_transport_parameters`, and
  `quic_network_harness`.
- TLS-on: `http3_network_owner`, `quic_interop_tls`, `quic_recovery_tls`,
  `quic_tls_handshake`, and `quic_server_admission`.
- All 19 programs returned zero. The sizing/recovery suite runs 15 cases and
  463 checks, including replayed Initial/coalescing loss and reordering,
  failed large discovery with successful 1200-byte CRYPTO traffic, black-hole
  fallback with late STREAM alias ACKs, and discovery across path activation.
- `cpplint` passed on all 26 changed C++ files; `git diff --check` passed.

The task card records completed implementation items and remains In Progress;
validation, runner-owned commits, final task status and publication belong to
the caller's later phases.
