# libhttpserver v3.0 architecture

**Status:** Draft 0.1 (reviewed)

**Owner:** Sebastiano Merlino

**Audience:** Maintainers, contributors, library consumers, distro packagers

**Date:** 2026-09-28

**Scope:** `PRD-V3N-REQ-001..038` in `specs/product_specs.md`

**Version boundary:** This document describes v3.0. `specs/architecture/01-*` through `14-*` and DR-001..014 describe v2.0; their MHD-specific implementation decisions are historical, not v3 requirements.

## 1. Executive summary

v3 is a C++20 embedded server whose HTTP/1.0, HTTP/1.1, HTTP/2, HTTP/3, QUIC, and WebSocket engines are owned by libhttpserver. Protocol engines convert wire events into one request-scoped `exchange`; routes, hooks, authentication, request bodies, responses, and errors operate on that semantic exchange. The production library has no libmicrohttpd dependency. A TLS-disabled build has no third-party runtime dependency beyond platform and C++ runtimes. A TLS-enabled build uses one OpenSSL 3.5 LTS provider for TLS, X.509, randomness, and QUIC packet cryptography; libhttpserver owns QUIC transport and HTTP/3.

The canonical handler is `task<void>(exchange&)`, with a clearly named bounded synchronous value-returning route adapter for small endpoints. An exchange reaches a terminal response or WebSocket upgrade once. Awaitable reads and writes expose backpressure; cancellation and application resume signals wake suspended work. A reusable response definition is immutable and replayable, while each send owns a fresh cursor and request-specific fields. HTTP/3, all required WebSocket modes, certificate replacement, and ACME TLS-ALPN-01 gate v3.0, not a later release.

## 2. Drivers and constraints

| Driver | Architectural response |
|---|---|
| One route across enabled HTTP versions | RFC 9110 semantic request/response model above distinct wire engines |
| No MHD; no runtime dependency except optional TLS provider | Owned parser, codecs, scheduler, QUIC, WebSocket, auth, forms; OpenSSL isolated privately |
| Bounded memory under slow/malicious peers | Hierarchical limits, admission, per-stream credit, bounded queues, timeouts, fair scheduling |
| External loop and four OS families | Private operation-oriented I/O boundary; internal epoll/kqueue/IOCP and fallback poll; public readiness adapter |
| Safe public lifecycle and ownership | Header-time decision, typed results, stop request separate from drain wait, explicit body-resource leases |
| v2 behavior parity with full API break | Behavior inventory and migration matrix; no v2 ABI or source shim |

The existing Autoconf/Automake packaging remains the initial v3 build path; build/test tooling may add dependencies but installed headers and production binaries obey the runtime rules. C++20 and a new SOVERSION remain. No cloud service, persistent store, auto ACME client, WebSocket extension, h2c, or unspecified future HTTP version is part of the server.

## 3. System shape

```text
application route / hook / auth / WebSocket handler
                  ↓
public server + exchange + body reader/writer + response definition
                  ↓
route/admission/lifecycle services + semantic HTTP stream
          ↙                ↓                  ↘
 HTTP/1 connection   HTTP/2 connection   HTTP/3 connection
 octet/framing       frames + HPACK       frames + QPACK
 TCP/TLS             TCP/TLS              owned QUIC v1 + UDP
          ↘                ↓                  ↙
  private operation I/O + timer/scheduler + optional OpenSSL adapter
                  ↓
  epoll / kqueue / IOCP / poll/WSAPoll; external readiness adapter
```

Each connection has one serialized protocol-state owner; request handlers may run concurrently on configured workers. State-changing operations from handlers return to the connection owner. Shared route tables, IP policy, hooks, and TLS snapshots publish immutable generations so readers need no global hot-path lock. This is a target responsibility split, not a direct transplant of v2's MHD callback services.

### 3.1 Public semantic interface (illustrative, not final signatures)

```cpp
using handler = unique_function<task<void>(exchange&)>;
result<void> server::route(method, path_pattern, handler);
result<void> server::route_sync(method, path_pattern,
                               value_handler, body_limit);
const request_head& exchange::head() const; // raw_target, route_path, method,
                                             // protocol, ordered fields, TLS/peer metadata
result<body_reader> exchange::admit_body(body_policy);
task<result<void>> body_reader::read(byte_sink); // bounded; trailers after EOF
task<result<bytes>> body_reader::collect(size_t maximum);
task<result<void>> exchange::respond(response); // one-shot terminal action
result<response_definition> response_definition::owned_bytes(status, fields, bytes);
result<response_definition> response_definition::reopen_file(status, fields, file_spec);
result<response_definition> response_definition::factory(status, fields, body_factory);
task<result<void>> exchange::respond(const response_definition&,
                                     response_overlay); // terminal, reusable
result<response_writer> exchange::start_response(status, fields);
task<result<void>> response_writer::write(bytes);
task<result<void>> response_writer::finish(fields trailers);
task<result<websocket_session>> exchange::upgrade(websocket_options);
resume_signal exchange::make_resume_signal(); // application-event suspension
stop_token exchange::cancellation() const;
send_result websocket_session::try_send(message); // accepted / backpressured / closed
task<result<void>> websocket_session::writable();
task<result<message>> websocket_session::receive();
void server::request_stop() noexcept; // nonblocking and handler-safe
result<drain_ticket> server::begin_drain(deadline);
task<drain_result> drain_ticket::wait(); // rejects a wait from counted handler
```

`task` is library-defined C++20 ABI: single consumer, move-only, executor-affine, lazy-start only when scheduled/awaited, with owned coroutine frame. Cancellation completes pending operations once with a typed result; handler exceptions are caught at the route boundary, logged, and converted to a 500 only before headers commit. Once a response is committed, failure resets/aborts the stream or closes the connection as required by the protocol. No exception crosses a socket callback. Handler return without terminal action synthesizes a defined internal-error response. `route_sync` adapts into the same exchange, automatically admits and buffers a body only up to its declared cap, and is documented as unsuitable for unbounded uploads or duplex work.

Headers and trailers preserve wire order and repeated names. Lookup is case-insensitive by field name; `append`, `replace`, `first`, and `all` have stable semantics. The raw target preserves received bytes; a separately validated and normalized route path is used for matching. `method` and `protocol` are libhttpserver types, including an extension-method value without backend IDs. Semantic content excludes HTTP/1 chunk markers and HTTP/2 or HTTP/3 frame bytes. A route sees complete headers before body delivery, then can reject, admit, await an application signal, or accept a valid upgrade. `Expect: 100-continue` is emitted only after admission. Pre-admission bytes are bounded; rejecting a body either drains a strictly bounded amount or closes/resets as the version permits.

Only one body read and one response write may be outstanding per exchange. Reads release HTTP/2 and QUIC receive credit only as bytes are consumed. Writes await bounded queue capacity, not peer acknowledgement. A response body source returns data, end, or error through typed results; no sentinel values or backend flags. Cancellation is fanned out to body operations, application suspension, response production, and WebSocket work. An application resume signal is idempotent, thread-safe, and becomes cancelled if its exchange ends; deadlines apply while suspended.

### 3.2 Response and resource ownership

An ordinary `response` is one-shot. `response_definition` is immutable and shareable only if its body is replayable: owned bytes, a reopenable file descriptor/path policy, or a factory creating a new source per send. Each send owns serialization state, body cursor, cancellation, and optional overlay fields/trailers. A `response_overlay` owns its ordered headers and trailers; they append after definition fields for that send only, and conflicting singleton/framing fields fail validation before headers commit. Concurrent sends create independent body sources and never mutate the definition or another send. A pipe is a unique one-shot source. File and pipe handles either transfer as `owned_file`/`owned_pipe` or are explicitly borrowed with a lifetime lease; borrowed memory requires a lease spanning send completion. Reuse attempts on non-replayable sources fail before writing. Request-specific overlays never mutate the shared definition. Response framing is selected by the protocol engine.

### 3.3 Protocol engines and WebSocket

| Engine | Connection-owned state and rules |
|---|---|
| HTTP/1.0 and HTTP/1.1 | Incremental octet parser, one authoritative message-boundary decision, keepalive/pipelining order, chunking/trailers, `100-continue`, and version-specific close behavior. Reject ambiguous `Transfer-Encoding`/`Content-Length`, invalid lengths, obsolete folding, and malformed field syntax; close where RFC 9112 requires. |
| HTTP/2 | TLS ALPN `h2`, frame and SETTINGS machine, connection-owned HPACK tables and ordered header blocks, per-stream and connection windows, stream cancellation, fair output scheduler, GOAWAY. No h2c. |
| QUIC v1 | UDP connection-ID demux, version negotiation, anti-amplification, path validation and rebinding, packet protection, CRYPTO reassembly, three packet-number-space loss/PTO state, congestion control and pacing, stream reassembly, flow control, and close/drain state. Packet records reference retransmittable information; packet bytes are never replayed as retransmission. 0-RTT is disabled for v3.0; session resumption may remain enabled. |
| HTTP/3 | ALPN `h3`, QUIC streams, one control stream and QPACK encoder/decoder streams per endpoint, connection-owned QPACK and blocked-stream accounting, limits and scheduler priority for critical streams, stream reset and GOAWAY. QUIC v1 is the required wire transport; QUIC v2 is not a v3.0 gate. |

The WebSocket codec consumes an ordered full-duplex byte stream. HTTP/1.1 uses Upgrade; HTTP/2 and HTTP/3 use negotiated Extended CONNECT on one stream. It validates masking, fragmentation, UTF-8, control frames, ping/pong, message limits, and close handshake. A typed session reports accepted/backpressured/closed on `try_send`; pending sends can await `writable()`. The close notification fires once with the best available reason. Extensions are not negotiated in v3.0. A WebSocket on HTTP/2 or HTTP/3 does not take over other streams.

### 3.4 I/O, concurrency, and external loop

Private `io_operation` objects describe accept, receive, send, timer, wake, and cancellation completions with storage alive through completion. Native backends use epoll on Linux, kqueue on BSD/macOS, IOCP on Windows, and poll/WSAPoll as fallback and test oracle. Backend delivery order never defines protocol order; the connection owner serializes effects. Readiness backends drain to would-block and rearm correctly; IOCP backends track overlapped-operation ownership.

External-loop mode exposes opaque socket keys with generations, readable/writable interest changes, a wakeup handle, the next deadline, and nonblocking `dispatch(events, now)`. The application registers interests in its own compatible readiness loop, calls `dispatch` on readiness or timer expiry, then applies the next interest snapshot. Stale generations are ignored. No application handler runs under an internal I/O lock. Dispatch is non-reentrant for a given server; route workers may still execute concurrently. The Windows external-loop path uses readiness registration (for example WSAPoll), while the managed internal loop may use IOCP.

Limits are hierarchical: server, listener, connection, and stream budgets for connections, streams, header bytes/fields, body buffers, response queues, QPACK/HPACK tables, blocked headers, QUIC reassembly, WebSocket messages, timers, and workers. Admission refuses work before capacity is committed; control-plane processing retains reserved capacity while data flow is backpressured. All public option combinations are validated before listeners accept. Timeouts cover handshake, header, body idle, application suspension, write idle, WebSocket close, and drain.

### 3.5 Lifecycle

`request_stop()` is idempotent and returns immediately, including from a handler. `begin_drain(deadline)` stops new listener admission and returns a ticket; waiting on that ticket from work counted by the drain reports `would_deadlock`. The state sequence is running → quiescing → draining → stopped. HTTP/1 finishes ordered in-flight work and signals close. HTTP/2 and HTTP/3 issue staged GOAWAY and drain accepted streams. WebSockets send Close, wait until peer close or deadline, then cancel remaining sessions once. QUIC retains closing/draining CID state for the protocol period. Deadline expiry cancels remaining work and reports incomplete work; destruction never silently waits for a handler on itself.

## 4. TLS, authentication, and security

The TLS adapter uses OpenSSL 3.5 LTS. Build configuration checks the QUIC TLS callback API; the exact minimum patch release is set against security advisories during implementation and release validation. OpenSSL performs TCP TLS and QUIC's TLS 1.3 handshake/secret exchange. Libhttpserver owns QUIC packet/header protection assembly, transport parameters, and all HTTP engines, using OpenSSL's EVP/HKDF/AEAD primitives. TLS-off builds exclude OpenSSL and reject TLS, HTTP/2, and HTTP/3 configuration with a libhttpserver error before listening; HTTP/1 and WebSocket-over-HTTP/1 remain available.

Certificate/SNI/client-auth/PSK policy is configured per listener or host before handshake. The TLS registry publishes a validated immutable snapshot atomically; new TCP/QUIC handshakes take the new snapshot, established connections retain their prior context. Default-host behavior, trust roots, mTLS mode, and external PSK identity are explicit. Client certificates are requested at the initial handshake; QUIC post-handshake authentication is unavailable. External PSK is a separate authentication profile from certificate/mTLS where TLS 1.3 cannot combine them. Its provider-neutral `psk_lookup(identity, handshake_context) -> result<secure_bytes>` is captured in the immutable TLS snapshot. It runs on the handshake executor outside I/O locks and may run concurrently for independent handshakes; applications must make their lookup safe for that concurrency. `secure_bytes` has a bounded length, moves into the handshake, and zeroes storage after use. Lookup rejection, timeout, or error rejects the handshake with a typed, redacted diagnostic; identity and key material are never logged. The v2 PSK identity-to-key behavior remains available on HTTP/1 TLS profiles where negotiated TLS permits it; HTTP/2 and HTTP/3 advertise PSK only for compatible TLS/ALPN profiles. Unsupported combinations fail configuration validation. No 0-RTT application data is accepted in v3.0.

The application supplies a short-lived ACME TLS-ALPN-01 challenge certificate and key (or the data needed to construct them), keyed by exact SNI. The registry validates the name, single SAN, critical ACME identifier extension, key/cert match, and expiry before publication. It selects this credential only for a TCP port 443 ClientHello with exact `acme-tls/1` ALPN and matching SNI; normal HTTP and QUIC ALPN cannot select it. Removal publishes another snapshot; existing handshakes finish safely. Certificate issuance, ACME account management, and challenge orchestration remain with the application.

Basic and Digest authentication, form/multipart parsing, route and hook policy, IP controls, file delivery, and SHOUTcast have owned v3 equivalents. Digest nonce generation and replay checks use OS randomness and versioned algorithm policy. A TLS-off build implements only the small required in-tree hash primitives for WebSocket handshake and documented Digest algorithms, with published test vectors and differential tests; OpenSSL supplies TLS/QUIC cryptography in TLS-on builds. No user-supplied header or path is reused in a second message-boundary parser. Per-route auth and hooks run at defined phases without gaining access to wire-layer state.

## 5. Build, observability, and validation

Installed headers expose no MHD, OpenSSL, OS socket, or backend enum types. TLS-on/off builds expose the same declarations and a feature report; unavailable operations return typed libhttpserver errors. The v3 SOVERSION and migration guide make the complete API break explicit. No MHD headers, binaries, linker flags, or runtime probes remain. Build and package checks run on Linux, BSD, macOS, and Windows (including the project's supported Windows toolchain); a TLS-off package links only platform/C++ runtimes, and a TLS-on package links only OpenSSL's `libssl`/`libcrypto` project as its third-party runtime provider.

The library emits structured diagnostic callbacks for accept/reject, protocol and TLS errors, stream cancellation, drain, and resource-limit events, without a logging runtime. Optional counters expose active connections/streams, queued bytes, flow-control stalls, handshake failures, and cancellation. Callbacks are bounded/nonblocking or dispatched outside I/O locks; sensitive fields, credentials, and body content are excluded by default.

Release gates:

1. Independent-client HTTP/1.0/1.1, HTTP/2, HTTP/3, and WebSocket-over-1/2/3 round trips, multiplexing, loss/reordering, cancellation, flow control, trailers, and drain.
2. RFC conformance, parser differential and request-smuggling corpora, HPACK/QPACK and QUIC state-machine fuzzing, malformed input, and ASan/UBSan/TSan; bounded-memory tests with slow readers and writers.
3. TLS ALPN, SNI, mTLS, PSK profile, resumption with 0-RTT disabled, hot certificate replacement across existing and new TCP/QUIC connections, and exact ACME challenge selection/removal.
4. Public-header and dependency audits for TLS-on/off builds; Linux/BSD/macOS/Windows integration and packaging; external-loop tests for stale events, timers, wakeups, and reentrance.
5. A v2-v3 behavior inventory and executable parity corpus for routing, hooks, Basic/Digest auth, form/multipart uploads, file/pipe/borrowed responses, IP controls, TLS features, WebSocket, and SHOUTcast. Every intentional behavior change receives a migration note. The v2 source API has no compatibility promise.

## 6. Decision records

### DR-V3-001: Own the wire engines and use one semantic exchange

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-001..011, PRD-V3N-REQ-017..027, PRD-V3N-REQ-037 require no MHD and one route behavior across three versions.

**Options considered:** (1) One protocol-neutral exchange above owned, separate wire engines: deep interface and local protocol changes, but significant implementation. (2) Three public request APIs: simpler engines, divergent handlers. (3) Keep a replaceable HTTP backend: smaller initial implementation, violates no-dependency and backend-free API goals.

**Decision:** Option 1. Connection-owned HPACK/QPACK/QUIC state stays below semantic streams.

**Rationale:** One semantic route contract meets cross-version behavior without exposing wire state; separate engines keep compression and transport rules local.

**Consequences:** Protocol interop and security testing are release critical. Supersedes v2 MHD backend decisions and DR-014's MHD callback composition for v3.

### DR-V3-002: OpenSSL 3.5 LTS is the sole optional TLS/crypto provider

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-002..003, PRD-V3N-REQ-007, PRD-V3N-REQ-034..036 require dependency-minimal QUIC TLS, certificate policy, and ACME.

**Options considered:** (1) OpenSSL 3.5 LTS: official third-party QUIC TLS callback API and one crypto project, with young API to test. (2) GnuTLS 3.8: low-level handshake hooks but more QUIC glue and mandatory Nettle/GMP dependencies. (3) Write TLS/crypto in-tree: unacceptably large security scope.

**Decision:** Option 1. Exact patch floor follows current security advisories at implementation/release.

**Rationale:** OpenSSL provides the required QUIC TLS callback interface and cryptographic primitives through one external provider.

**Consequences:** A TLS-on build uses OpenSSL privately; v2 GnuTLS configuration migrates to provider-neutral options. TLS-off remains dependency-free.

### DR-V3-003: Coroutine exchange plus bounded synchronous route adapter

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-009, PRD-V3N-REQ-021..027, PRD-V3N-REQ-031 and the small-endpoint JTBD require both streaming control and concise routes.

**Options considered:** (1) Coroutine exchange only: smallest surface, more ceremony for simple routes. (2) Canonical coroutine exchange plus named sync adapter: same engine, simpler common case, two documented route modes. (3) Lifecycle callbacks: no coroutine ABI but larger per-application state machines. (4) Value-returning handlers only: easy common case but suspension, duplex and backpressure require special cases.

**Decision:** Option 2. The sync adapter has an explicit body cap and shares exchange lifecycle.

**Rationale:** The exchange makes backpressure and cancellation explicit, while the adapter preserves a short path for bounded endpoints.

**Consequences:** Public `task` scheduling, cancellation, lifetime, and exception rules become ABI contracts. Replaces v2's response-return/deferred handler model for v3.

### DR-V3-004: Operation-oriented private I/O, readiness-shaped external integration

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-014..016 and four OS families need external loop support while IOCP and Unix backends differ.

**Options considered:** (1) Private operation boundary plus public readiness/timer adapter: portable, moderate surface; Windows external mode uses readiness. (2) Public completion-driver interface: native IOCP integration, substantially larger host contract. (3) Poll/WSAPoll for all modes: simple, less scalability headroom.

**Decision:** Option 1.

**Rationale:** One small external readiness contract works across the supported platforms without constraining the private native I/O backends.

**Consequences:** Generation-tagged interests and dispatch semantics need cross-platform tests; internal managed mode can use IOCP without exposing it.

### DR-V3-005: Immutable reusable response definitions, fresh per-send state

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-026..030 require replayable concurrent reuse and explicit file/pipe/borrowed lifetimes.

**Options considered:** (1) Immutable definition plus per-send cursor/source factory and overlays: safe reuse, more types. (2) Share a mutable response/cursor: small surface, races and ambiguous ownership. (3) Copy response per send: simple semantics, potentially expensive and cannot duplicate one-shot sources.

**Decision:** Option 1.

**Rationale:** Per-send cursors and owned overlays prevent concurrent sends from sharing mutable body or field state.

**Consequences:** Non-replayable sources remain one-shot; borrowed resources need explicit lease.

### DR-V3-006: Strict, connection-owned protocol state and hierarchical budgets

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-004..008, PRD-V3N-REQ-021, PRD-V3N-REQ-027 and the bounded-memory acceptance gate.

**Options considered:** (1) Per-connection serialized state with per-stream work, credit accounting, and limits: clear ownership but scheduler complexity. (2) Per-stream independent parsers/compressors: easy concurrency, invalid HPACK/QPACK and QUIC ownership. (3) One global protocol lock: simple correctness, cross-connection contention.

**Decision:** Option 1. QUIC v1 with 0-RTT disabled is the v3.0 baseline.

**Rationale:** Protocol compression and flow-control state belong to their connection, and 0-RTT adds replay policy without serving a v3 requirement.

**Consequences:** State-machine, fairness, memory, and timer tests gate release.

### DR-V3-007: Immutable TLS credential snapshots and exact ACME selection

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-034..036 require live rotation and temporary ACME credentials.

**Options considered:** (1) Validate and atomically publish immutable registry snapshots: established sessions safe, snapshot lifetime cost. (2) Mutate shared TLS contexts in place: smaller registry, unsafe concurrent behavior. (3) Restart listeners on change: simple, violates uninterrupted connections.

**Decision:** Option 1; ACME only on matching TCP SNI plus `acme-tls/1`.

**Rationale:** Snapshot publication preserves active handshakes and connections while exact SNI/ALPN selection isolates challenge credentials.

**Consequences:** Validation and handshake concurrency tests gate release.

### DR-V3-008: Split handler-safe stop initiation from drain completion

**Status:** Accepted. **Date:** 2026-09-28.

**Context:** PRD-V3N-REQ-031..033 directly reverse v2 DR-008's stop-in-handler deadlock contract.

**Options considered:** (1) Nonblocking request plus separate deadline-bound ticket: safe in handlers, explicit coordination. (2) Blocking stop everywhere: self-deadlock. (3) Immediate close: loses active work and graceful protocol behavior.

**Decision:** Option 1.

**Rationale:** A handler can request shutdown without waiting for its own completion, while an external coordinator can observe the deadline outcome.

**Consequences:** v3 supersedes DR-008's `stop()` exception; staged GOAWAY and WebSocket close need test coverage.

## 7. Requirement coverage and migration

| PRD requirements | Owning design / gate |
|---|---|
| 001..003, 037 | Dependency boundary, OpenSSL adapter, public results, package audits |
| 004..009 | Separate HTTP/1, HTTP/2, QUIC/HTTP/3 engines, one exchange and route table, interop |
| 010..013 | Shared WebSocket codec, three handshake adapters, send/close semantics |
| 014..016 | Semantic options, private I/O, external readiness adapter, pre-listen validation |
| 017..020 | Ordered fields/trailers, raw target and validated route path, owned identifiers |
| 021..025 | Header-time decision, bounded body reader, collect cap, resume signal, cancellation |
| 026..030 | Typed body source/writer, bounded output, explicit leases, immutable reusable definition with overlays |
| 031..033 | Stop/drain state machine and WebSocket close policy |
| 034..036 | TLS policy profiles, immutable credential registry, ACME TLS-ALPN selection |
| 038 | Behavior inventory, parity corpus, explicit migration notes |

v2 DR-001 (C++20) and DR-011 (SOVERSION policy) carry forward in principle. v2 DR-008 (handler stop), DR-010 (deferred/WS ownership), DR-013 (MHD-backed Digest behavior), DR-014 (MHD callback services), and MHD-specific architecture sections are v2-only. The migration guide maps each documented v2 operation to its v3 equivalent or records its intentional behavior change; it cannot claim source compatibility.

## 8. Open risks

| ID | Risk | Closure before v3.0 |
|---|---|---|
| AR-V3-01 | QUIC/HTTP/3 implementation and interop scope is substantial | Stage vertical slices behind build flags during development; full protocol gate before release |
| AR-V3-02 | Young OpenSSL third-party QUIC TLS API | Version-gated adapter, official API compatibility matrix, handshake fuzz and interop |
| AR-V3-03 | Public coroutine ABI and external-loop wakeup semantics can freeze mistakes | Compile small consumer examples and race/cancel tests against a written state contract before implementation |
| AR-V3-04 | No-TLS Digest and WebSocket hashing is security-sensitive in-tree code | Fixed algorithm inventory, test vectors, differential and fuzz tests |
| AR-V3-05 | Existing v2 docs and tests contain MHD-specific behavior | Inventory before migration; version-scope docs and parity tests, with explicit exceptions |

## 9. References

- Product requirements: `specs/product_specs.md` §3.9.
- HTTP semantics and versions: [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html), [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html), [RFC 9113](https://www.rfc-editor.org/rfc/rfc9113.html), [RFC 9114](https://www.rfc-editor.org/rfc/rfc9114.html); [HPACK RFC 7541](https://www.rfc-editor.org/rfc/rfc7541.html); [QPACK RFC 9204](https://www.rfc-editor.org/rfc/rfc9204.html).
- QUIC transport, TLS, recovery, v2: [RFC 9000](https://www.rfc-editor.org/rfc/rfc9000.html), [RFC 9001](https://www.rfc-editor.org/rfc/rfc9001.html), [RFC 9002](https://www.rfc-editor.org/rfc/rfc9002.html), [RFC 9369](https://www.rfc-editor.org/rfc/rfc9369.html).
- WebSocket and Extended CONNECT: [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html), [RFC 8441](https://www.rfc-editor.org/rfc/rfc8441.html), [RFC 9220](https://www.rfc-editor.org/rfc/rfc9220.html); ACME TLS-ALPN-01: [RFC 8737](https://www.rfc-editor.org/rfc/rfc8737.html).
- OpenSSL third-party QUIC TLS callbacks: https://docs.openssl.org/3.5/man3/SSL_set_quic_tls_cbs/.
- OS I/O: https://man7.org/linux/man-pages/man7/epoll.7.html and https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports.
