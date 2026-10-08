# TASK-157: QUIC byte credit and protected critical storage

This private, provider-free foundation lives in `libhttpserver_v3core.la`.
It composes stream state, semantic consumption receipts, bounded numeric credit,
and reliable recovery information. It does not add an HTTP/3 parser, application
exchange, congestion controller, pacer, or UDP connection engine.

## Owner and wire invariants

A serialized connection owner keeps one `quic_flow_control`. It copies initial
numeric limits from validated local and peer transport parameters. The four
stream classes use the advertiser's perspective for asymmetric bidirectional
limits: a locally initiated stream sends against peer bidi-remote credit and
receives against local bidi-local credit. Unidirectional send/receive eligibility
continues to use the existing stream state and frame direction rules.

Local stream-count exhaustion returns `blocked`. A peer's excessive ordinal
returns `stream_limit_error` before high-water counters or descriptors change.
A sparse higher peer ID opens implicit lower IDs without materializing their
stream objects. Numeric records are charged and reserved before allocation;
there are at most `max_records` records (hard configuration ceiling 65536).
They retain closed-stream facts for the whole connection and are never evicted.
The owner must keep each materialized stream object stable through retirement,
and must never recreate a retired ID. Capacity exhaustion returns a local
`capacity` refusal, rather than misclassifying it as a peer violation.

Accepted receive credit counts each stream's highest offset, including FIN and
RESET final size; the connection counts the sum of increases. Duplicates, gaps,
and overlap therefore never charge a previously accepted extent twice. Checks
precede stream storage admission, and the ledger commits only after stream
validation and reassembly succeed. Conflicting overlap, invalid final size,
allocation refusal, and storage/budget limits leave byte counters unchanged.
Stream state remains the authority for final-size consistency and directions.

Incoming MAX values increase absolute send limits; smaller/equal values do
nothing. MAX_STREAM_DATA validates direction and creation/count state. BLOCKED
frames never replenish credit and cannot grow unbounded records. Arithmetic is
checked at the 62-bit QUIC integer ceiling; receive-window growth saturates
there. The wire contracts follow RFC 9000 sections 4, 4.5, 19.9–19.14.

## Semantic retirement

`quic_stream_state::read()` extracts raw transport bytes into a parser or body
staging area. It releases reassembly storage, but does **not** release QUIC byte
credit. The same unreleased transport extent still bounds staged body data.
The future HTTP/3 adapter must budget its staging allocations as well.

`consume_body(stream, through_offset)` reports ordered transport offsets whose
associated body content has actually been consumed by the application.
`consume_protocol` retires protocol-only bytes using that same release cursor.
Neither receipt may cross earlier unconsumed body content. The adapter supplies
this semantic fact; the numeric ledger cannot infer it from parsing. Receipts
must be between the previous release cursor and the extracted offset. Equal
receipts release zero; backward or unextracted offsets return
`invalid_consumption`. Both connection and stream targets advance exactly once.
A stalled body retains its original window while an independently consumed body
can advance its own stream credit and the connection's shared credit.

RESET first accounts final size and discards receive storage. `settle_reset`
then retires abandoned credit once at connection scope, stops stream-credit
updates, and preserves the opposite send half. STOP_SENDING does not retire
receive credit. Count replenishment requires terminal retirement of all
applicable halves; bidirectional receive consumption alone is insufficient.
New stream-count credit stops when the descriptor table has no remaining room
for additional retained facts. Retirement does not reclaim those facts.

## Recovery composition

The constrained `prepare_packet(..., flow)` overload skips blocked STREAM work,
splits payload to available byte credit, validates RESET against the highest
committed send extent, and continues selecting critical work. FIN bounds later
eligibility. `quic_send_plan` exposes the selected STREAM, RESET, or flow-control
snapshot. Preparation and failed/abandoned emission charge no committed send
credit. `commit_sent` charges exactly the selected extent without allocation;
loss or probes of previously emitted extents charge zero. The flow owner must
outlive an outstanding preparation, and the connection owner must not mutate its
send ledger independently between preparation and emission/commit. The legacy
unconstrained overload remains available for existing standalone recovery users.

`retain_flow` retains only MAX_DATA, MAX_STREAM_DATA and the two MAX_STREAMS
classes. Controls carry owned numeric snapshots. Supersession reuses a bounded
descriptor with a fresh information ID, so loss and late ACKs of old packets
cannot restore an old value or acknowledge a newer generation. Completed MAX
facts remain bounded and retained, preserving monotonic coalescing across other
admission. STREAM cancellation and space discard release copied payload/status
storage once. ACK history for collected critical packets uses preallocated,
coalesced ranges; it distinguishes committed packet numbers from abandoned
numbers while freeing critical records behind stalled ordinary packets.

The semantic owner queries `pending_credit()` or a named
`pending_credit(kind, stream_id)` to obtain outstanding targets independently.
It passes each target to `retain_flow`, calls `credit_emitted(plan.flow)` only
after successful packet emission, and calls `credit_acknowledged` with the flow
snapshot from `take_completion()` after recovery reports delivery. Desired,
emitted and acknowledged values are separate monotonic facts. Abandoned output
and dropped packets retain reliable work in recovery. Consumption queues credit
without waiting for a BLOCKED frame. RESET/retirement should cancel any retained
stream MAX information that is no longer useful.

## Protected storage arithmetic

Recovery configuration reserves explicit `critical_information`,
`critical_retained_bytes`, and `critical_sent_packets` within existing total
quotas. Each must fit its corresponding total. Ordinary STREAM admission cannot
spend these quotas. CRYPTO, MAX, and RESET information has selection priority;
ACK-only packets use critical records and are selected ahead of pending STREAM
payload when ACK work exists. Data proceeds when eligible critical work drains.
Default zero reservations preserve the older standalone API; a composed owner
must configure nonzero reservations for its required critical workload.

Logical quotas alone do not protect shared ancestor memory. `quic_storage_pool`
reserves a connection envelope against the supplied budget and every ancestor
before accepting storage owners. Its independent, bounded data/critical roots
subdivide that already charged envelope, avoiding double charges at original
ancestors. The sum must fit the existing resource ceiling. No public resource
keys or general budget API change. Pool construction reports invalid arithmetic
with `invalid_argument` and allocation/admission refusal with `bad_alloc`.

A connection computes its envelope using the actual storage helpers:

```text
D = receive_stream_bound * quic_reassembly::storage_capacity(stream_limits)
    + 2 * (recovery.max_retained_bytes - recovery.critical_retained_bytes)
    + bounded parser/body staging storage
C = quic_flow_control::storage_capacity(max_records)
    + quic_recovery::storage_capacity(recovery)
    + 2 * recovery.critical_retained_bytes
    + configured CRYPTO/TLS storage
```

Each reassembly helper includes its exact descriptor table and twice the maximum
payload, covering old-plus-staged merges. Recovery metadata includes every
space's receive ranges, packet records, collected-critical-packet ranges, and
information descriptors. The factor two for retained recovery payload accounts
for owned bytes and per-byte delivery status. QUIC TLS storage is three input
reassembly envelopes, three configured output capacities, receive-lease capacity,
maximum peer parameters, and copied local parameters; the TLS callback storage
helper calculates and validates this sum. Caller sums/products must be checked
before constructing the pool. Storage-owner objects, allocator bookkeeping,
provider-internal TLS allocations, and fixed key objects are outside these
existing requested-buffer accounting quotas.

Pass `pool.data()` leases to receive STREAM storage and the recovery data side;
pass `pool.critical()` to the flow ledger, recovery critical side and CRYPTO/TLS
owners. Leases retain the ancestor reservation until the last covered owner and
all its buffers are destroyed, even when the pool handle goes away first. Always
pass the lease to a covered owner, not a detached copy of its internal budget.
Direct-budget constructors retain their older ancestor-charging behavior and
must not be mixed with detached internal pool budgets. Envelope configuration
can refuse undersized storage locally; it never spills data into critical roots.

## Local evidence

Ignored `build/task157/` contains RED/GREEN and sanitizer receipts. The baseline
stream suite passed before implementation. Failure stubs produced 5 failed
flow-ledger checks and 5 failed recovery checks. Protected-storage stubs produced
8 failures. Subsequent RED cases caught lost MAX history, ACK starvation,
recreated stream retirement, post-FIN send eligibility, invalid RESET selection,
and missing independent credit snapshots. TLS callback and session lifetime
oracles each caught a prematurely released ancestor envelope before correction.

Final C++20 strict standalone suites pass: flow ledger 8 cases / 167 checks,
flow/recovery composition 9 cases / 235 checks, protected storage 4 cases /
51 checks, and TLS callback/storage 8 cases / 204 checks. All four pass again
with AddressSanitizer and UndefinedBehaviorSanitizer and no diagnostics. The
reused OpenSSL provider is not sanitizer instrumented.

Autotools TLS-on builds the C++20 v3core and passes all 13 focused executables,
including the affected codecs, reassembly/state, ACK/recovery/repacketization,
real independent-client TLS handshake, and genuine outbound-CRYPTO recovery
seam. TLS-off builds provider-free v3core and passes all 10 focused executables.
The new independent-client TLS handshake also completes while the data budget is
fully reserved, and its protected ancestor reservation outlives the pool handle.
The composition suite bounds stalled body A while body B extracts and consumes
partially, then admits additional credited data. Data payload, descriptors,
packet records and actual ancestor capacity are saturated independently while
bounded CRYPTO, ACK, MAX and RESET work remains selectable.

Commands (from the task worktree):

```sh
bash bootstrap
mkdir -p build/task157-on
(cd build/task157-on && ../../configure --enable-v3-tls --disable-examples \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g')
make -C build/task157-on/src -j3 libhttpserver_v3core.la
make -C build/task157-on/test -j3 quic_flow_control quic_flow_recovery \
  quic_flow_storage quic_stream_state quic_reassembly quic_ack_state \
  quic_recovery quic_repacketize quic_frame quic_transport_parameters \
  quic_tls_session quic_tls_handshake quic_recovery_tls
make -C build/task157-on/test check \
  check_PROGRAMS='quic_flow_control quic_flow_recovery quic_flow_storage quic_stream_state quic_reassembly quic_ack_state quic_recovery quic_repacketize quic_frame quic_transport_parameters quic_tls_session quic_tls_handshake quic_recovery_tls' \
  TESTS='quic_flow_control quic_flow_recovery quic_flow_storage quic_stream_state quic_reassembly quic_ack_state quic_recovery quic_repacketize quic_frame quic_transport_parameters quic_tls_session quic_tls_handshake quic_recovery_tls'
make -C build/task157-on check-local
```

TLS-off uses `build/task157-off`, `--disable-v3-tls`, no provider flags, and the
same suites except the three TLS executables. Sanitizer reproduction commands
are the task-local `run-focused.sh`, `run-storage.sh`, `run-tls.sh` with
`SAN_FLAGS='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer'`; the
standalone flow command uses the same flags and flow/state/reassembly/frame/
varint sources. All standalone commands use `-Wall -Wextra -Werror -pedantic`.

Changed-file cpplint, CCN <= 10, warning-suppression and whitespace checks pass.
Full-repository complexity, duplication and file-size findings match a fresh
HEAD source export: 17 existing complexity findings, three existing duplicate
runs and the existing 509-SLOC `io_poll_backend.cpp` finding. No thresholds or
unrelated code were changed. Autotools builds emit existing bootstrap and macOS
linker warnings; standalone strict builds are clean.

Task status remains In Progress for caller-owned validation and finalization.
BSD, Windows and other nonlocal checks remain assigned to CI/the v3 PR under
AGENTS.md. These are bounded foundation/composition proofs, not end-to-end
HTTP/3 or UDP QUIC interoperability evidence.
