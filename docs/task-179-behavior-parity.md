# TASK-179 behavior parity audit

The audit replays all 29 committed v2 cases through production v3 dispatch and
real private transport owners. The six modes are HTTP/1.0 and HTTP/1.1 over TCP
and TLS, HTTP/2 over TLS, and HTTP/3 over QUIC. Golden expectations and recording
provenance remain unchanged. The bounded inventory includes every original
capability and completion-map row, plus certificate, SNI, client-certificate and
external-PSK outcomes: 60 capabilities, 360 protocol cells.

## Protocol applicability

The two HTTPS cases require encrypted transport. HTTP/1.0 has no supported
WebSocket Upgrade mechanism; its WebSocket handshake and echo cells are excluded
with that explicit rationale. TLS record and credential capabilities likewise
require encryption. All other golden cells execute. There are 168 applicable
corpus observations: 26/27 clear HTTP/1.0/1.1, 28/29 TLS HTTP/1.0/1.1, and 29 each
for HTTP/2 and HTTP/3. Applicability exclusions have their own recorded outcomes.

## Protocol translations

HTTP/1.0 requests explicitly request keepalive. Known-length bodies retain it;
unknown-length pipe/deferred bodies use EOF framing and close the connection.
This is the HTTP/1.0 migration of HTTP/1.1 chunking and persistence expectations.
Only request header blocks are translated; binary/multipart payloads are intact.

HTTP/2 and HTTP/3 use numeric status, lowercased field names, DATA framing and
stream completion. The comparator still checks application status, exact body,
Content-Length when present, Content-Type, Allow, auth challenges, captures,
hook fields, repeated-field order and initial-header/trailer separation. Request
Connection fields and fields they nominate are removed. Methods, target/query,
authority, ordered application fields and binary body are preserved. Pipelining
becomes two stream-correlated responses on one connection; connection-wide
response ordering is an HTTP/1 contract. Every persistent corpus exchange has a
successful ordinary follow-up on that same connection.

ICY is asserted literally on HTTP/1. The same marked response becomes numeric
200 with the same body on HTTP/2/3; no synthetic ICY status line is recorded.
WebSocket uses the pinned fixed-key 101/Accept handshake on HTTP/1.1, and Extended
CONNECT numeric 200 without Upgrade/Accept fields on HTTP/2/3. All supported
modes verify independent masked text echo and close 1000; multiplexed modes
also verify stream completion and an ordinary sibling request on the connection.

The aioquic 1.3.0 client omits outgoing HEAD context when checking response
Content-Length. The bounded client adapter records HEAD streams, permits their
representation length with zero DATA and rejects any HEAD DATA. Other methods
retain the original length validator. A regression verifies all three cases;
the received headers and body are never rewritten.

## Routing migrations

v3 registration order replaces v2 literal-before-parameter tiers. Register
literal siblings first to retain v2 precedence. Prefix depth and catch-all
behavior remain. Regex routes and segment regex constraints move into application
handlers or a prefix wrapper. The named registration/family tests verify the
current surface, rather than claiming removed regex overloads execute.

## Hook migrations

The native bus retains seven request phases plus listener accept_decision.
MHD connection-open/close notifications and body_chunk are omitted; use typed
admission, request completion, transport peer snapshots and the handler read loop.
Per-resource hooks become route-handler wrappers. Alias setters become page
factories, auth guards and response/completion hooks. Throwing or invalid page
factories degrade to default 404/405; throwing exception hooks preserve the
contained empty-body 500. Named ordering, snapshot and containment tests supplement
the real hook corpus over every applicable transport.

## Peer policy migrations

The native policy uses typed outcomes and peer_refusal reasons, literal/CIDR and
trailing IPv4 wildcards; middle wildcards are rejected. Mapped IPv4 normalizes,
forwarded headers do not change identity, and unspec identity has explicit rules.
Each request revalidates its immutable transport peer against the live policy.
The wire probes admit loopback, mutate the policy, and verify the next request
has zero application response bytes: EOF on HTTP/1, stream reset on HTTP/2/3.
In-flight responses complete before the next refusal. H2's snapshot comes from
getpeername; H3 uses the UDP owner's admitted/validated peer. Existing H3 tests
also verify snapshot stability across validated rebinding.

## Request and source migrations

v3 retains raw escaped target/query, ordered fields and separate trailers; it
has no merged v2 query/form/path map. Applications opt into bounded decoders;
path captures use exchange::path_args(). Cookies use the owned `http::cookie` value,
`http::parse_cookie_header` and typed `http::render_set_cookie` operations with
ordered Cookie/Set-Cookie fields. TASK-192 supplies the installed helper and the
`http_cookie` suite with all three capability-054 identities. Receipts bind named
checks to the actual executable/hash, command, complete log and executed counts;
semantic supporting evidence does not claim per-protocol wire observations. Request bodies are pulled and footers
are accessed after end-of-body. Malformed encodings and limits return typed errors.
Multipart disk sinks have explicit abort cleanup, keep-once callbacks and sanitized
names, verified by named decoder and disk lifecycle tests.

Files are really opened; missing files map before commit to sanitized 500, empty
files stay empty. Owned pipes are one-shot; borrowed bytes require a retained lease;
deferred producers are per-send factories. Named source/definition tests verify
concurrency, replay, retained storage and failure cleanup. These common semantic
tests are labeled semantic; H2/H3 framing suites are labeled engine-frame, while
the golden body observations come from TCP/TLS/QUIC owners.

## TLS capabilities

Certificate generations, SNI/ALPN selection, optional/required client authentication
and external PSK have named provider/BIO and real TLS socket policy evidence.
TLS record segmentation is provider-owned and never was a pinned application
contract; fragmented authenticated I/O is verified. External PSK remains HTTP/1
only: server_options::check_psk_protocols rejects H2/H3, tested by
server_options_validate:psk_limited_to_http1. PSK lookup bounds, cancellation,
identity policy and provider retry behavior are exercised, rather than skipped.
TLS-off server_options tests prove typed encrypted-feature unavailability and do
not count as successful encrypted transport observations.

Public server::listen() still rejects TLS/H2/H3 before binding. The named
native_server:unsupported_transports_rejected_before_any_bind test records this
release integration gap. Private owner evidence proves protocol/application
behavior; it does not claim public encrypted listener availability. Full public
listener integration is outside this audit's scope.

## Repaired defects and TDD evidence

H2/H3 formerly used run_route and lost captures, 405/Allow/default body and hook
composition. Their parity regressions failed four checks per protocol before
both engines adopted dispatch_request and a stream-owned lifecycle_sink. The
optional internal owner-lifetime context supplies hooks, page factories, policy
and transport peer. Protocol head refusal suppresses successful response_sent.
Cancelled queued/parked requests now complete exactly once via the existing
lifecycle guard; separate cancellation regressions reproduced the missing
notification. Historical RED logs: build/task179-off/{h2,h3}-red.log and
build/task179-on/{h2,h3}-cancellation-red.log.

HEAD sync values previously attempted a payload write and broke persistence.
route_sync:head_value_keeps_length_without_writer_traffic failed eight checks
across the four protocol versions before retaining representation length while
committing without body traffic. Historical RED/GREEN logs remain under
build/task179-on/focused-results/route_sync-head-*.log.

HTTP/1 TLS needed a private tls_socket_backend composing encrypted application
operations with raw timers, wakeups and socket ownership. Its native corpus also
exposed WebSocket handoff cancellation aborting the whole established TLS session.
The new plaintext-read cancellation regression failed six checks, plus the TLS
WebSocket corpus. The private socket owner now requests a nonfatal serialized
plaintext-reader cancellation policy; default TLS adapters retain session-abort
semantics, and control/write cancellation and deadlines still abort. Both direct
and operation-based cancellation preserve established I/O, and the independent
TLS WebSocket handshake/echo/close passes. RED/GREEN:
build/task179-on/tls-read-cancel-{red,green}.log.

The live runtime-deny probes exposed a further H2/H3 defect: semantic peer
refusal emitted no wire reset and retained stream capacity. Both focused exchange
regressions failed the reset and single-slot reuse checks; the actual TLS/QUIC
clients timed out. The stream reapers now emit REFUSED_STREAM / H3_REQUEST_REJECTED
for that completed, refused exchange. They release capacity without application
headers/body and preserve exactly-once peer_refused completion. RED logs remain
under build/task179-probes/{h2-policy-engine,h3-policy-unit,h3-policy-engine}-red.log.

The request-sequence helper was tested before implementation for pipeline splitting,
binary bodies, malformed lengths, bounds and multiplexed field translation. The
fail-closed receipt checker has mutation tests for missing/duplicate capabilities,
corpus additions, cells, outcomes, logs, exits, malformed/unfinished observations,
skips/defects, stale input hashes, named checks and prohibited semantic waivers.
Named tests cannot be replaced by a process starting or exiting zero.

## Reproduction and retained evidence

Worktree: /home/etr/progs/libhttpserver/.worktrees/TASK-179; branch task/TASK-179;
base v3 at 7a0e850d23308ade4da0a1b2648f4a7bd0c2e80f. Local tools: Ubuntu
Clang 18.1.3, C++20, native provider OpenSSL 3.5.9; independent hyper-h2 4.4.1
and aioquic 1.3.0 in build/task179-clients. Both lanes use Autotools and
CXXFLAGS='-std=c++20 -O0 -g', --disable-examples; TLS-off uses --disable-v3-tls,
TLS-on uses --enable-v3-tls and provider headers/libraries from
/home/etr/progs/libhttpserver/build/tooling/provider.

Run from the worktree, choosing a fresh receipt directory:

```sh
python3 scripts/run-v3-behavior-parity.py \
  --tls-on build/task179-on --tls-off build/task179-off \
  --client-python build/task179-clients/bin/python \
  --openssl /home/etr/progs/libhttpserver/build/tooling/provider/bin/openssl \
  --receipt-dir build/task179-audit
python3 scripts/run-v3-behavior-parity.py --receipt build/task179-audit/receipt.json
```

The runner rebuilds both native cores and required suites, executes named checks,
then executes the actual corpus owners/clients. receipt.json retains exact commands,
working directories, exits, executable/provider identities, source/corpus/inventory
hashes, all per-case responses, named capability evidence and protocol outcomes.
The final passing receipt is build/task179-audit-final-passed/receipt.json. It contains
168 transport observations, 90 named supporting observations and 360 outcomes
(152 preserved, 192 explicit migrations, 16 applicability exclusions).
The TLS-off lane executes 34 suites / 477 tests / 4195 checks; TLS-on executes
44 supporting suites / 546 tests / 6244 checks, plus the native corpus suite's
15 tests / 55 checks. All required native checks have zero failures/skips.
Python contracts add 29 receipt tests and 3 HEAD-client tests. Both make check-local
lanes, changed-file cpplint and distribution verification pass. Existing default
hyper-h2 and aioquic smoke matrices also pass. The ordinary legacy replay passes
9 checks with one optional libmicrohttpd_ws skip; it uses explicit loopback
NO_PROXY/no_proxy so CURL does not route local fixture traffic through the proxy.

Each command log, client observation JSON, H3 fixture log and PCAP stays in the
fresh receipt directory. Failed attempts remain separately under
build/task179-probes and build/task179-{on,off}. Legacy transcript_runner optional
feature skips remain baseline evidence, not native transport passes.

The existing peer_policy shared-mutex stress case can starve its two writers under
six busy reader threads on unrestricted Linux scheduling. The first bounded attempt
is retained. The audit records explicit single-CPU taskset scheduling for that suite;
all threads and checks execute unchanged (9 tests, 64 checks).

Local C++20 builds, changed-file lint, make check-local in both lanes and distribution
verification gate completion. BSD, Windows and other nonlocal checks were not
executed; CI and the v3 PR own them under repository policy. No validation agents,
finalization, staging, commit, merge or publication were performed in implementation.
