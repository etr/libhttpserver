# TASK-156 QUIC recovery ownership and local evidence

This task adds a private, provider-free `quic_recovery` owner. It implements
RFC 9000 section 13 ACK state and RFC 9002 sections 5–6 recovery; it does not
create a QUIC connection engine, congestion controller, or UDP scheduler.

Normative references:
- https://www.rfc-editor.org/rfc/rfc9000.html#section-13
- https://www.rfc-editor.org/rfc/rfc9002.html#section-5
- https://www.rfc-editor.org/rfc/rfc9002.html#section-6

## Owner contract

The connection serializes every call and supplies `steady_clock` timestamps.
The recovery owner never reads a clock, sleeps, calls a TLS provider, retains
ciphertext, or registers a backend timer. Initial, Handshake and Application
have independent receive histories, packet numbers, sent aliases and loss
deadlines. RTT and PTO backoff are shared. All application key generations
share Application space. Application frame preparation uses 1-RTT semantics;
early-data policy is outside this task.

After authentication, call `inspect_received` before applying frames. Fresh
packets are committed with `receive_packet` only after frame acceptance;
duplicates do not repeat frame effects. Calling `receive_packet` for an
ack-eliciting duplicate requests a prompt ACK. Retired packet numbers must
never be processed again. The bounded descending range history merges adjacent
ranges and advances an irreversible floor when its oldest range is dropped.
Acknowledgement of a packet carrying an ACK retires history through that
packet's receive watermark, except when a new eliciting receipt filled a hole
after that ACK was prepared. Such receipts retain pending ACK work until a
current ACK is acknowledged. Rejected packets are not acknowledged.

Initial/Handshake ACKs are immediate. Application ACKs are delayed at most the
local maximum, with immediate feedback on gaps, reordering and every second
ack-eliciting packet. Non-eliciting traffic cannot create an ACK loop. Local
ACK delay/exponent configuration is independent of peer recovery parameters.
`prepare_ack` measures delay from the largest receipt, including receipt at
time zero. Its generation is published only after successful emission;
failed output/emission preserves pending ACK work. `prepare_packet` appends
fresh ACK content when it fits, leaving it pending when it does not.

Retain owned CRYPTO, STREAM (including empty FIN), or immutable RESET_STREAM
information. Other reliable control kinds are not exposed by this facade.
Each handle describes one immutable segment. An ACK for any transmission
covers the shared information slice, including an original packet acknowledged
after loss or replacement preparation. Lost replacement aliases never
resurrect delivered or cancelled data. Completion is emitted once per handle,
including the FIN/control terminal. Callers use `delivered_prefix` for partial
contiguous CRYPTO delivery, and `take_completion` for stream/reset completion.
They explicitly cancel superseded segments, including STREAM information
superseded by a RESET_STREAM. Consumed completions and cancelled handles may
be reclaimed on the next retention admission; callers retain their own stream
or TLS delivery ledger beyond that point.

`prepare_packet` selects a bounded pending slice, splitting payloads to fit
output and setting FIN only on the final slice. A probe prefers unsent data,
then undelivered transmitted data, then PING. No preparation marks information
sent or delivered. One preparation is outstanding at a time. Record capacity
is reserved before encryption: protect with the returned packet number, then
`commit_sent` with the actual emission timestamp/wire length, or
`abandon_packet` on failure. All allocated packet numbers are burned, including
abandoned/short preparations, so an encryption nonce is never reused.
Successful commit performs no allocation. Prepared information may become
delivered or cancelled while waiting for emission without recreating pending
information on commit. The owner must supply valid monotonic timestamps,
apply send permission and current keys before emission, and abandon cancelled
transport preparations before discarding their packet-number space.

Standalone `reserve_packet` supports caller-generated ACK/PING/control packets.
`commit_sent` validates its token, emission flags and length. Application key
generation acknowledgements are returned as facts to the caller's key owner.
Decoded ACK ranges are fully validated before any delivery/RTT/accounting
mutation. Unsent numbers detectable above the retired sent floor are rejected;
repeated ACKs and old ranges below that floor produce no duplicate events.

RTT samples require a newly acknowledged largest packet and at least one newly
acknowledged ack-eliciting packet. The minimum is unadjusted. The first sample
initializes the estimator, including a sample taken at time zero. Initial and
Handshake ignore ACK delay; before handshake confirmation Application also
ignores delay. Confirmed Application delay is capped before shifting, avoiding
integer overflow. Subsequent estimates use 1/4 RTT variance and 1/8 smoothed RTT
updates. Loss uses packet threshold 3 and time threshold
`max(9/8 * max(latest, smoothed), 1ms)`, only in the acknowledged space.

Call `set_environment` when key availability, handshake confirmation, send
permission or peer validation changes. In particular clients supply their
initial peer-validation fact and time to arm the anti-deadlock timer. Local
amplification permission is distinct from whether the peer validated this
endpoint. Servers implicitly regard peer validation as complete; a Handshake
ACK also establishes it. `next_deadline` coordinates ACK work with recovery.
A loss deadline takes priority over PTO and remains active when sending is
blocked. Application PTO is ineligible before confirmation. PTO initially uses
333ms RTT and 166.5ms variance (999ms timeout); confirmed Application adds peer
maximum ACK delay. Backoff doubles and saturates safely. `expire` reports due
ACK spaces, loss bytes, or one/two probe requests without declaring PTO packets
lost. Late expiry rearming uses the expiry time to avoid repeated immediate
probe requests. Any newly acknowledged packet resets backoff once the peer
has validated this endpoint, including ACK-only and PADDING packets. This
reset is independent of RTT sampling; duplicate ACKs and unvalidated-client
Initial ACKs preserve backoff. A new Handshake ACK establishes peer validation.
Space discard reports discarded bytes separately from loss, cancels its
information/preparation/history and resets backoff.

## Storage and future transport integration

The existing hierarchical `quic_reassembly_bytes` budget charges the bounded
receive/sent/information descriptor tables at construction and twice each
retained payload length (owned bytes plus one delivery-status byte per byte).
No allocation depends on a packet number or stream offset. Limits also bound
sent records, information handles and total retained payload. Failed budget
admissions roll back their reservations. Invalid configuration throws;
allocation/budget failure for the tables leaves a refused owner returning
`no_memory`. Payload admissions return typed `capacity`/`no_memory` results.
Live lost aliases are never evicted to admit replacements: exhaustion applies
backpressure until delivery/cancellation frees them. Receive history and fully
retired sent prefixes can be reclaimed within their explicit floors.

Backend timer registration and its `timers` budget remain with the future
transport scheduler. The caller composes `quic_key_state::protect_packet` and
`open_packet`, TLS `copy_output`/`retire_output_prefix`, and stream sent/ACK
state transitions with this owner's facts. Wire byte events account only
packets still in flight; late ACKs of lost packets deliver information without
counting their bytes twice. No congestion policy is implemented here.

## Reproduction

The exact existing worktree/branch were verified as `task/TASK-156`, based on
`v3` at `66f6fb32c1502b86f7b7b1e0f7b5a8d045aab550`. It was clean before changes.
Autotools/Littletest baseline: the pre-existing frame, packet, stream-state and
reassembly executables passed (4/4). No staging, commits, merging, cleanup or
other Groundwork phase was performed by implementation.

```sh
./bootstrap
mkdir -p build/task156-off
cd build/task156-off
../../configure --disable-v3-tls --disable-examples \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -C src -j3 libhttpserver_v3core.la
make -C test -j3 quic_ack_state quic_recovery quic_repacketize \
  quic_frame quic_packet quic_stream_state quic_reassembly
make -C test check \
  check_PROGRAMS='quic_ack_state quic_recovery quic_repacketize quic_frame quic_packet quic_stream_state quic_reassembly' \
  TESTS='quic_ack_state quic_recovery quic_repacketize quic_frame quic_packet quic_stream_state quic_reassembly'
make check-v3-native-linkage
```

The separate TLS-on directory uses:

```sh
mkdir -p build/task156-on
cd build/task156-on
../../configure --enable-v3-tls --disable-examples \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -C src -j3 libhttpserver_v3core.la libhttpserver_v3tls.la
make -C test -j3 quic_ack_state quic_recovery quic_repacketize quic_recovery_tls \
  quic_key_state quic_crypto quic_tls_session quic_tls_handshake \
  quic_frame quic_packet quic_transport_parameters
make -C test check \
  check_PROGRAMS='quic_recovery_tls quic_ack_state quic_recovery quic_repacketize quic_key_state quic_crypto quic_tls_session quic_tls_handshake quic_frame quic_packet quic_transport_parameters' \
  TESTS='quic_recovery_tls quic_ack_state quic_recovery quic_repacketize quic_key_state quic_crypto quic_tls_session quic_tls_handshake quic_frame quic_packet quic_transport_parameters'
make check-v3-native-linkage
```

The provider header was rechecked: OpenSSL 3.5.9 (29 Sep 2026). Native linkage
audits passed in both modes. Existing macOS linker flags produce deprecation
and duplicate-runtime-library warnings; the changed C++ sources compile without
compiler warnings.

## TDD and local results

Behavioral REDs were observed using minimal facade stubs, followed by their
implementations and GREEN reruns. The initial standalone linker probe needed
the existing `quic_varint.cpp` dependency before the first behavioral RED.
The reusable early-cycle command was:

```sh
clang++ -std=c++20 -DHTTPSERVER_COMPILATION -Isrc -Itest \
  test/unit/quic_ack_state_test.cpp src/detail/quic_ack_state.cpp \
  src/detail/quic_frame.cpp src/detail/quic_frame_write.cpp \
  src/detail/quic_varint.cpp -o /private/tmp/task156-ack
/private/tmp/task156-ack
```

Subsequent cycles substitute `quic_recovery_test.cpp` or
`quic_repacketize_test.cpp` and include `quic_recovery.cpp`,
`quic_recovery_timer.cpp` and `quic_repacketize.cpp` as introduced. The final
recovery suite additionally links `support/quic_network_harness.cpp` and the
existing native core for the test-only drop/reorder/duplicate rig.

| Cycle | Observed RED | Observed GREEN |
| --- | --- | --- |
| Receive ranges/spaces, scheduling, floor, output | 3 failing tests | 38 checks passed |
| Sent records, malformed/unsent ACKs, capacity | 3 failing tests | 26 checks passed |
| RTT and loss threshold boundaries | 12 failed checks | 54 checks passed |
| PTO/lifecycle and timer eligibility | 10 failed checks | 93 checks passed |
| Owned information, loss aliases, splitting, FIN/reset | 5 failing tests | 66 checks passed |
| Duplicate eliciting packet rearms ACK | 1 failed check | 50 checks passed |
| First Application gap and due ACK work | 2 failed checks | 56 checks passed |
| Legal peer max_ack_delay above one second | 1 failed check | 119 recovery checks passed |
| Allocation failure while reporting budget refusal | 1 failed check | 143 repacketization checks passed |
| Non-eliciting receipt during ACK preparation/emission | 2 failed checks | 62 ACK checks passed |
| ACK-of-ACK retirement after a new reordered receipt | 3 failed checks | 155 repacketization checks passed |
| Retained segment exceeding codec packet size still splits | 1 failed check | 163 repacketization checks passed |

Final tests also cover partial probe delivery followed by original loss, alias
capacity refusal preserving late ACK delivery, failed send publication,
acknowledged receive-watermark retirement, all three spaces generating fresh
CRYPTO replacements, payload and allocation budget rollback, and a bounded network harness
scenario with a dropped packet, reordered delivery and a duplicate. The TLS
seam test obtains genuine outbound CRYPTO without using the helper's eager
retirement path, protects original/replacement packets with new numbers,
opens/decodes the replacement, and retires TLS output only through a contiguous
acknowledged prefix after out-of-order ACKs.

Local gates: TLS-off 7/7 executables; TLS-on 11/11 executables, zero skips.
New suites: ACK 7 tests/62 checks, recovery 14 tests/119 checks,
repacketization 12 tests/163 checks, TLS seam 1 test/35 checks.
Changed files pass cpplint and the per-function CCN <= 10 gate. Standalone
AddressSanitizer/UndefinedBehaviorSanitizer builds of ACK and repacketization
suites pass, including extreme clock arithmetic; these do not claim to
instrument the complete native engine or TLS provider.

Full-repository static probes exposed unchanged baseline findings: 17
complexity violations in existing IO/header code, three existing CPD duplicate
runs, and `io_poll_backend.cpp` at 509 SLOC against the 500-line ceiling. None
involves TASK-156's files. They were recorded without expanding repair scope.
The task's implementation is ready for caller-owned validation; its status
remains In Progress. BSD, Windows, other nonlocal platform checks and complete
HTTP/3 transport interoperability were not run locally. Nonlocal checks belong
to CI/the v3 PR under the user policy in AGENTS.md.
