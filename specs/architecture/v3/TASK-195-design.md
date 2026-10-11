# TASK-195 native TCP HTTP/2 ownership

The accepted `connection_engine` retains its TLS transport, admission and
retirement lease. After a successful enabled certificate/mTLS ALPN selection of
`h2`, it owns `http2_tcp_owner`. No public server or provider API is added, no
live socket is moved to another listener record, and no private fixture enters
the production transport. TLS-off and public H3 remain rejected before bind.

The H2 owner composes the existing engine and native read/write/wake/timer
operations. Its serial `http2_dispatch` processes one queued invocation per
worker turn; parser turns retain their existing frame quantum. Input suffixes
survive yields. Only one write borrows the immutable engine output, and only
the actual transferred bytes advance it. Existing CONTINUATION and exposed
control retirement remain the protocol engine's responsibility.

Each admitted stream owns a separate serial affinity on configured workers.
That affinity exclusively owns the exchange, handler frames, lifecycle hooks,
resume state and completion/destruction. The wire owner alone touches HPACK,
stream maps, windows, pending frames and deadline anchors. Handler-side
respond/start/upgrade prepares owned bounded state in one semantic slot before
publishing acceptance. The owner applies that state in stream order. Terminal
publication has an independent path; resetting a queued stream closes its
entry gate before it can invoke application code. Resetting a running stream
queues cancellation behind the active resume and retains every frame until it
returns. Cancellation never concurrently writes the running exchange.

The body stream uses fixed rings and short stream-local locks. Read/write
waiter publication and owner progress notifications happen after unlocking.
An atomic consumed counter transfers exact application consumption to the
owner; receive/send windows remain owner state. Published failure references
refer to immutable outcomes, avoiding a reference race during terminal
publication. Request trailers are exposed only after the handler sees EOF;
response trailers are immutable once finish is published.

The scheduler has sixteen bounded work slots and a separate overflow terminal
callback. Progress callbacks coalesce owner work. Queued capture destruction
happens outside the scheduling lock. An overflow closes the stream/connection
through its terminal callback, rather than silently dropping a live decision.
No owner or stream lock spans hooks, application code, executor post, external
readiness dispatch or destruction of user captures.

Server → listener → connection → stream budget children copy and clamp parent
capacities. Stream seats, request copies, rings, response fields/trailers and
WebSocket codec storage belong to the stream child. Parser/HPACK/control and
one active output frame belong to the connection. Header staging reservations
transfer into admitted stream state without a reservation gap. Existing TLS
admission and retiring provider work retain their leases. Bounded reserves are
made before synchronous acceptance; refusal propagates through typed failure
and lifecycle cancellation. A full semantic queue produces terminal failure.

`next_deadline` inventories absolute SETTINGS/header, stream body-idle,
suspension, write-idle, WS close and native drain deadlines. The native watchdog
arms the actual earliest timer and cancels/rearms only when its governing
deadline changes. Header block completion clears its anchor after parser frame
release; otherwise an otherwise idle completed HEADERS frame could incorrectly
expire the connection before the stream body timer. Body idle starts at body
admission, and output advances the write anchor only on actual transfer. No
periodic polling timer is added.

Native H2 streams count in the server drain scope. A stream affinity publishes
the counted drain identity, so handler waits return `would_deadlock`. Stop is
nonblocking. Drain stages GOAWAY, refuses openings beyond the accepted cutoff,
lets accepted work finish and expires remaining requests/WS through the same
terminal path. Tickets outlive server destruction and retain the original
incomplete result and remaining count after deadline expiry. Connection
finalization waits for transport loop and actual stream retirement.

Certificate/mTLS ALPN binding tracks H1.1 and H2 eligibility transactionally.
Live publication cannot enable a protocol disabled by an attached listener.
PSK retains H1-only policy. TLS1.2 H2 rejects prohibited CBC ciphers before
request hooks. Existing ACME exact SNI/port/ALPN isolation remains enforced.

Alternatives refuted by the plan and retained RED: one shared handler/wire
executor lets a running handler stall controls; substituting a worker pool
violates exchange cancellation affinity. A global protocol mutex would hold
unrelated streams behind application code. The chosen private owner plus
stream-local semantic handoff preserves the public synchronous API and shared
route/lifecycle services. The original serialized private-engine mode remains
available for deterministic corpus and H3-shared helper users.

The implementation evidence maps each doubt to behavioral RED/GREEN and the
installed independent matrices. Local Linux gates own normal and sanitizer
proof; BSD/macOS/Windows remain explicitly CI/v3-PR owned under AGENTS.md.

Required H1 parity verification also exposed a shared terminal-publication race:
shutdown published an ordinary disconnect flag before its immutable reason, and
H1 read the final disconnect state after relinquishing its lifetime lock. The
minimal common repair publishes a copyable atomic flag after the reason/body/
writer metadata and snapshots final H1 disconnect state under the existing
connection lock. It preserves public value/move semantics and adds no shared
protocol lock. A terminal-response/resume-versus-shutdown regression proves
exactly-once completion under TSan; H2 affinity cancellation remains unchanged.
