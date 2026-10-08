# TASK-153: QUIC admission and amplification ownership

The listener now has a private serialized admission controller. Known CIDs use
TASK-149's owner-affine dispatcher, preserving retired and queue-full outcomes.
Unknown packets must have valid canonical endpoints, bounded invariant headers,
and a UDP datagram of at least 1200 bytes. Unsupported nonzero versions receive
Version Negotiation based only on those invariants; replies swap the CIDs,
advertise v1, and randomize the unused first-byte bits. Version zero gets no reply.
Unknown v1 packets must be strictly parsed, authenticated client Initials.

The default policy sends stateless Retry before retaining pending state. Retry
uses a random nonempty server CID of the listener's configured length and the
existing v1 Retry integrity codec. A client's original DCID may have a different
length. Tokenless Initial DCIDs must have at least eight bytes. The optional
direct policy requires a DCID compatible with the listener's registration length
and retains an **unvalidated** path; Initial AEAD success never validates it.

| Resource | Default / bound |
| --- | --- |
| Pending attempts | 64 |
| Retained packet allocation capacity | 1 MiB, separately from the entry cap |
| Pending lifetime | 10 seconds, expired at the boundary |
| Registered CIDs / retirement history | 4096 each, existing dispatcher |
| Server CID length | 8 bytes, configurable from 1 to 20 |
| Retry token | At most 84 bytes; accepted through issue time + 10 seconds |
| Send reservations per budget | 64, with monotonically increasing generations |
| Stateless response | At most 160 allocated packet bytes, one send reservation |

Retry tokens contain format byte `1`, purpose byte `0x52`, an eight-byte issue
time, one-byte-length-prefixed original DCID and Retry SCID, and a 32-byte
HMAC-SHA256 tag. Integers use network order; no padded structs are serialized.
The authenticated context also includes the canonical peer address/port/scope,
optional local endpoint and interface index, receiving socket identity, and
listener identity, with explicit presence markers. A listener owns a dedicated
random 32-byte `secure_bytes` secret. Explicit 32-byte provisioning supports
reproducible tests. EVP MAC/provider failures fail closed; tag comparison is
constant time, and claims publish only after verification. Wrong paths, listeners,
Retry CIDs, expiry, future times, trailing bytes, truncations, and modifications
are rejected without another Retry challenge. Retry's public integrity key is
separate from this private token secret.

A budget belongs to one canonical path. IPv4 padding and irrelevant scope are
normalized; IPv6 scope, ports, local endpoint, interface and socket stay distinct.
Before validation, completed plus reserved sends cannot exceed three times
received UDP bytes. Receive accumulation saturates below multiplication overflow;
validated budgets still check debit overflow. Completion retains the debit even
if the transport's outcome is ambiguous. Cancellation refunds only a send proven
unsent, exactly once. Stale or cross-budget handles cannot refund replacements.

Call `receive` once per actual UDP datagram with immutable packet storage and
caller-supplied monotonic seconds in a consistent listener token epoch. Coalesced
packets receive one credit; separately received duplicates receive their actual
bytes. Deduplication uses canonical path plus destination/source CIDs and retains
one Initial per pending attempt. It neither extends expiry nor adds retention.
Admission-owned retained bytes count vector allocation capacity, including spare
capacity. Handles are weak generation identities; expiry, cancellation, failed
registration and failed queueing release their owned retention.

Use `inspect(handle)` to attach the facts and shared budget to the connection sink
**before** `promote`: an inline executor can deliver the retained Initial before
promotion returns. Promotion registers the CID and queues that Initial through
the existing owner port. Its bytes are already credited. Later routed datagrams
are accounted by the owner once. A different canonical path gets a separate
unvalidated budget, with no copied credit or validation. Facts preserve the
original DCID and current destination CID for later transport parameters. This
seam implements no migration or path-validation protocol. Stateless Retry/VN
responses have an independent one-datagram budget and exactly one consumable
reservation, without a growing pre-admission path table.

## Development and local verification

Assertion-level RED receipts precede production behavior: the amplification
stub failed 67 checks, token issuance failed both roundtrip assertions, and the
admission stub failed six checks. Additional RED cases caught uncharged retained
allocation capacity (three failed checks) and rejection of differing original
DCID/server CID lengths (one failed assertion). GREEN receipts are retained in
`/private/tmp/task153-{budget,token,admission,capacity,cid}-*.log`.

Final C++20 TLS-on focused checks passed 8/8 executables, no skips: amplification,
address tokens, admission, crypto, key state, packet codec, invariant header, and
dispatch. The new tests passed 89, 152 and 6349 assertions respectively, including
2000 distinct bounded direct attempts, 1000 stateless Retry responses, every
Initial/token truncation, token mutations, coalescing, independent paths, exact
send limits, stale handles, promotion and retirement. TLS-off passed 4/4 focused
executables (amplification, packet, invariant header, dispatch), and both native
linkage audits passed. UDP loopback checks used permitted local socket access;
the initial sandboxed baseline timed out, then passed outside that restriction.

ASan/UBSan passed all three new executables using the existing provider's static
archives; the provider itself is not sanitizer-instrumented. Changed-file cpplint,
CCN <= 10 and whitespace checks passed. Full repository complexity, duplication
and file-size outputs match a fresh HEAD export exactly after path normalization:
17 pre-existing complexity warnings, three unchanged clone groups and the existing
509-SLOC `io_poll_backend.cpp` finding. No thresholds or unrelated code changed.

Builds are task-local VPATH directories under `build/task153-{on,off,sanitize}`.
TLS-on uses the verified OpenSSL 3.5 installation at
`/private/tmp/task129-provider/install`, Homebrew include/library paths, and
`CXXFLAGS='-std=c++20 -O0 -g'`. Run `./bootstrap`, configure with
`--enable-v3-tls --disable-examples`, build `libhttpserver_v3core.la`, then build
and run the eight named focused executables and `make check-v3-native-linkage`.
TLS-off uses `--disable-v3-tls` without provider flags. Sanitizers add
`-fsanitize=address,undefined -fno-omit-frame-pointer` and explicit provider
`libssl.a`/`libcrypto.a` paths. Exact command logs are in `/private/tmp/task153-*`.

BSD, Windows and other nonlocal checks remain assigned to CI/the v3 PR by
AGENTS.md. The task stays In Progress pending caller-owned validation and
finalization; this phase does not claim handshake, HTTP/3 interoperability,
deployment or production behavior.
