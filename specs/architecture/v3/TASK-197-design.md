# TASK-197: public HTTP/3 lifecycle and RFC9220

The native server retains one public exchange and shared routing, hooks, authentication,
forms and response definitions across TCP/H1/H2 and UDP/H3. Application handlers run
on independent stream affinities. A bounded owned command transfers an RFC9220
opening response and driver/session to the serialized H3 wire owner; it never performs
QUIC/QPACK mutation in an application worker. Opening fields and resource reservations
survive the handoff. The receive half remains available for WebSocket DATA. Reset,
disconnect, timeout and drain cancel suspended receive/writable operations before the
handler frame and leases retire. Close deadlines start on the actual close transition.
Receive RESET and send STOP_SENDING remain independent even after semantic cancellation:
a late STOP still prepares exactly one RESET_STREAM with the actually emitted final size.

Ordinary HTTP response handling would abandon the upgrade receive half. Direct worker
mutation would put user handlers in the protocol serialization domain. The existing H2
owned-command model avoids both problems without introducing another runtime.

One application stream seat covers framing and semantic ownership. QUIC flow retirement
requires terminal receive state and reliable FIN/reset acknowledgement; handler return
alone cannot replenish transport capacity. Retiring transport records drops heavy receive
and send storage, accepts authenticated late credit/retransmission without recreating
work, and preserves a bounded 64-entry lifetime identity record. H3 admits at most 16
live requests. This fixed lifetime capacity is deliberate and does not promise unlimited
requests on one connection. Critical control/recovery capacity remains separately reserved.

Whole authenticated datagrams credit the identity's current application budget exactly
once, including a distinct validated migrated path. Initial precredit is preserved. Policy
is checked at admission, candidate discovery and activation; an accepted exchange retains
its copied peer identity while subsequent work sees the validated current path. A rejected
claimed admission remains in the existing bounded expiring table to prevent repeated hooks
for retransmitted Initial. No unvalidated path becomes an application path.

Aggregate drain keeps weak UDP listener owners alongside weak TCP owners, so deadline
cancellation reaches all listeners without creating a scope cycle. Admission completing
across the drain transition cannot publish a new connection. Failed H3 GOAWAY initiation
retains the original deadline. Application drain counts remain distinct from three-PTO
protocol retention, alias keys and pending atomic datagram completion. Handler initiation
returns promptly; waiting on a ticket counted by that handler returns `would_deadlock`.

Native H3 advertises the actual bounded QPACK table and blocked-stream capacities
(4096 bytes and eight sections). Installed independent clients withhold real encoder
instructions, decode cancellation/refusal and prove healthy siblings before release.
The bounded loss/reordering witness is a one-peer test datagram relay retaining at most
one 2048-byte packet; it neither decodes nor injects server protocol state.

Common public `100-continue` admission exposed an H2 omission. Successful admission
publishes a bounded informational command. Informational HEADERS do not mark final
HEADERS sent or permit DATA before the final head. The independent aioquic 1.3.0
witness has a test-only adapter resetting its header parsing state after a decoded 1xx
section, because the pinned implementation otherwise classifies the final HEADERS as
trailers. The installed package has no independent-client dependency.

Credential selection uses the existing immutable public store generations and QUIC
provider adapter. Initial mTLS, SNI, independent stores, held Finished across rotation,
existing/new connections, resumption and genuine rejected early-data attempts remain
public wire observations. ACME selection is TCP-only. PSK/H3, invalid ALPN and
post-handshake client authentication fail before publication/listening.

The installed consumer and native example compile from installed public headers and
libraries only, for shared/static linkage and managed/external loops. Public semantic,
RFC9220, aggregate drain and credential receipts supplement the retained nine-stage
transport/conformance proof. BSD, Windows, macOS and other nonlocal checks remain
owned by CI and the v3 PR. Validation/finalization and Git publication are runner-owned.
