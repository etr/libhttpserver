# v2 observable-behavior parity inventory (TASK-096 / TASK-120)

Version-scoped baseline of documented v2.0 behavior for the v3 native
HTTP/1 migration. This inventory pairs with the executable transcript
corpus in `test/parity/transcripts/*.tseq`, executed by the
segmented-input transcript runner (`test/transcript_runner.cpp`) against
the pluggable `server_fixture` (`test/parity/server_fixture.hpp`): the
v2 fixture today, v3-engine fixtures in later milestones, same corpus.

Scope and authority:

- **PRD-V3N-REQ-038** (specs/product_specs.md): documented v2 behavior
  is preserved through v3 equivalents unless a migration note records
  the change. Areas covered: routing, hooks, Basic/Digest auth,
  forms/multipart, file responses, IP controls, TLS, WebSocket,
  SHOUTcast.
- **Architecture gate**: specs/architecture/v3/architecture.md item 5
  (v2-v3 behavior inventory and executable parity corpus).
- **DR-V3-001** (native HTTP/1 engine); risk AR-V3-05.

## How to read the tables

One table per PRD-V3N-REQ-038 area. Columns:

- **Documented v2 behavior** — the observable contract on the wire.
- **Documentation source** — where v2 documents it (README.md,
  docs/architecture/*, or the v2 public-header contract in
  src/httpserver/).
- **Transcript case** — `file.tseq:case` exercising it, or `—` for
  inventory-only rows.
- **Baseline status** —
  *pinned* (committed transcript expectation recorded on v2),
  *inventory-only* (real behavior, deliberately not pinned), or
  *MHD-artifact* (v2-specific behavior that v3 is not required to
  reproduce; no transcript).
- **Migration note** — filled by later milestones when v3 changes the
  behavior; empty means "preserve".

Curation rule (plan D4): every committed expectation was recorded
against the running v2 server (`./transcript_runner --record`) and then
reviewed; anything unpinnable cross-engine became an inventory row
instead of a golden assertion. Volatile material (Digest nonce/opaque,
server wall-clock `Date`) is normalized — `<*>` masks and Date elision —
never pinned.

## Routing

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| Exact-path GET returns 200 `text/plain` body with Content-Length framing, connection stays open | README.md §Routing; src/httpserver/webserver_routes.hpp; docs/architecture/features.md | routing.tseq:get_hello | pinned | TASK-118: preserved on the single v3 registration surface `route_registry::route` (`httpserver/server/routes.hpp`; first registration-order full match); the corpus replays through the real registry + dispatcher + `http1_response_framer` (`routing_corpus`), socket truth in `native_http1_e2e`. |
| Parameterized exact paths (`/params/{id}/name/{name}`) bind captures as request args | docs/architecture/features.md (routing tiers); src/httpserver/webserver_routes.hpp contract | routing.tseq:parameterized_path | pinned | TASK-118: wire-identical (replayed); the delivery surface changes shape — captures arrive as first-class name/value pairs on `exchange::path_args()` (stamped by the dispatcher from `resolve`), not replayed into v2's flat `set_arg` arg map; `{name}` segment names match `[A-Za-z0-9_]+` and duplicate names are rejected at registration. |
| `method_set` GET+HEAD serves HEAD as a headers-only response; the GET body length is still declared via `Content-Length` and no body bytes follow (RFC 7231 §4.3.2) | RFC 7231 §4.3.2; v2 wire behavior | routing.tseq:head_both_methods | pinned | TASK-105: HEAD/no-content rules; TASK-118: a GET+HEAD registration is one `http::method_set` entry (`route(method_set, ...)`); HEAD replays through the framer's `head_no_body` with the GET body length declared, byte-identical to the pin. |
| Unregistered method on a registered path answers 405 with `Allow: <methods>` and the fixed body `Method not Allowed` | docs/architecture/errors.md (405 handling) | routing.tseq:method_not_allowed | pinned | TASK-118: preserved — `route_registry::resolve` returns a `method_miss` carrying the entry's merged method set and the dispatcher synthesizes the page (v2 default body, `text/plain`, auto `Content-Length` through the shared value-commit); `Allow` rides comma-space in `method_id` order on every 405, custom pages included; the `method_not_allowed_handler` alias mapping is recorded in the hooks table. |
| Miss path answers 404 `text/plain` body `Not Found` | docs/architecture/errors.md | routing.tseq:not_found | pinned | TASK-118: preserved — the default page is synthesized by the dispatcher through the same value-commit path (auto `Content-Length`); the `not_found_handler` alias mapping is recorded in the hooks table. |
| Two pipelined requests on one connection are answered in order; connection survives | docs/architecture/threading.md (connection handling) | routing.tseq:pipelined_keepalive | pinned | TASK-107: pipelined response order; TASK-118: the corpus case replays as two sequential dispatches framed into one wire stream (`routing_corpus`); the engine's own pipelined ordering is exercised by `native_http1_e2e`. |
| Prefix families (`register_prefix`) match every path under a prefix; `""`/`"/"` is the documented catch-all | README.md §Routing; src/httpserver/webserver_routes.hpp | — | inventory-only | TASK-118: preserved as `route_registry::route_prefix(method_set, pattern, handler)` — segments match a prefix of the request segments, equal length included, `"/"` catch-all; precedence: full matches beat prefix matches, among prefixes most segments wins, ties by registration order. |
| Three-tier lookup: the exact hash tier answers before the parameterized trie tier and the linear regex chain last — an exact literal beats a parameterized sibling regardless of registration order | docs/architecture/features.md (routing tiers); src/detail/route_table.cpp | — | inventory-only | TASK-118 sibling-precedence delta: v3 has one full-match class — among full matches (exact literal and parameterized alike) FIRST registration order wins, so a parameterized pattern registered before a literal sibling captures a request v2 answered from the exact tier; register exact literals before parameterized patterns to keep v2 outcomes. Prefix matches always lose to full matches; deepest-prefix-wins among prefixes is preserved. |
| `route()` accepts a `std::regex` pattern matched after the exact and parameterized tiers fail (linear chain) | src/httpserver/webserver_routes.hpp (regex overload); docs/architecture/features.md | — | inventory-only | TASK-118: the regex route family is NOT ported (plan D1) — the v3 `route_pattern` is structured segments only (literal or `{name}`), so the engine never compiles or evaluates user regexes; callers keep regex routing application-side (a `"/"` prefix route that re-dispatches on `route_path`, or an ordinary handler that matches and answers itself). |
| `{name|regex}` constrains a parameter segment to the embedded regular expression | src/httpserver/detail/http_endpoint.hpp (pattern vocabulary); docs/architecture/features.md | — | inventory-only | TASK-118: per-segment constraints are NOT ported (plan D1, same rationale as the regex family) — a `{name}` segment matches any single segment and the capture is delivered on `exchange::path_args()`; constraint checking belongs in the handler (answer 404/403 there), not in the route table. |

## Hooks

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `after_handler` hook mutation (`with_header`) lands on the wire | docs/architecture/hooks.md (after_handler) | hooks.tseq:get_hello_hook_header | pinned | TASK-118: preserved on the 7-phase v3 bus (`httpserver/server/hooks.hpp`) and replays byte-identical. Timing delta (plan D5): v3 handlers commit inside their task, so the phase fires at the engine-sink head-commit boundary — identical to v2 for value-shaped routes, at head-commit time (mid-handler) for streaming commits; and the phase mutates status/fields only, full response replacement is not offered. |
| `before_handler` short-circuit answers its response and skips the resource handler | docs/architecture/hooks.md (before_handler short-circuit) | hooks.tseq:before_handler_403 | pinned | TASK-118: preserved — the phase is also the consultation point when the method mismatches (a hook may supply the 405 and the engine still appends `Allow`); replays through the real bus + dispatcher (`hooks_corpus`). |
| A short-circuiting `before_handler` also suppresses `after_handler` (no hook header on the 403) | docs/architecture/hooks.md (phase order) | hooks.tseq:before_handler_403 (`expect header ~X-Hook`) | pinned | TASK-118: the v2 suppression matrix is preserved verbatim — `after_handler` fires on handler-return, synthesized-404, 405 and exception paths, never on `request_received`/`before_handler` short-circuits (the pinned `~X-Hook` absence replays green). |
| `not_found_handler` supplies the 404 body; the handler owns the status code (v2 does not override it) | docs/architecture/hooks.md (not_found alias); v2 wire behavior | hooks.tseq:custom_404 | pinned | TASK-118 alias→factory mapping: `not_found_handler` becomes the construction-time `server_options::not_found_response` factory (a `response_factory` set before `listen()`, not a runtime bus seat); the factory owns status and body exactly as the v2 alias did (replayed); TASK-120 consumes the mapping. Containment delta (PRD-V3N-REQ-038): a throwing, invalid or empty factory now degrades to the request's default 404 page (404 status preserved) -- an intentional containment delta, not parity: v2 left a throwing `not_found_handler` uncontained on the primary 404 path (the throw escaped into the daemon callback; the materializer's one contained call site degraded to the forced empty-body 500, never the default 404). |
| `method_not_allowed_handler` supplies the 405 body; handler owns the status; `Allow` still emitted | docs/architecture/hooks.md (method_not_allowed alias) | hooks.tseq:custom_405 | pinned | TASK-118 alias→factory mapping: `method_not_allowed_handler` becomes `server_options::method_not_allowed_response`; `Allow` is appended by the engine on every 405, custom body included (replayed). Containment delta (PRD-V3N-REQ-038): a throwing, invalid or empty factory now degrades to the request's default 405 page (405 status preserved, `Allow` included) -- an intentional containment delta, not parity: v2 caught a throwing `method_not_allowed_handler` inside the dispatch try and answered through the `handler_exception`/internal-error path (a 500-family answer, never the default 405 page). |
| The lifecycle is an 11-phase bus: request_received, body_chunk, route_resolved, before_handler, handler_exception, after_handler, response_sent, request_completed, connection_opened, connection_closed, accept_decision | src/httpserver/hook_phase.hpp; docs/architecture/hooks.md | — | inventory-only | TASK-119: v3 runs 8 phases on the native engine (`server::hook_phase`: accept_decision plus the 7 request-scoped phases request_received, route_resolved, before_handler, handler_exception, after_handler, response_sent, request_completed — v2 names kept where the native point exists, firing order preserved, short-circuit seats intact, server-wide registration order, snapshot-copy firing, zero-cost-when-unused, runtime-safe add/remove). `accept_decision` is ported (TASK-119): fires once per accepted transport on the listener after the peer-policy verdict is fixed, observation only, `accept_decision_ctx` carries the `net::peer_address` snapshot, the verdict, and the typed `peer_refusal` reason (the v2 reason names). Migration exceptions: `connection_opened`/`connection_closed` are MHD-notify artifacts and remain unported — the documented v3 per-connection observation seats are accept_decision (admission), request_completed (settle), and `exchange::peer()`; `body_chunk` is not ported — v3's pull-based body model puts pre-body control at `request_received` and per-chunk visibility in the handler's read loop (firing user code in the engine reader loop would violate the threading contract). |
| `http_resource::add_hook` attaches per-route hooks; server-wide hooks fire before per-route hooks, registration order within a scope | src/httpserver/webserver_hooks.hpp (http_resource::add_hook); docs/architecture/hooks.md | — | inventory-only | TASK-118: per-resource attachment is NOT ported — per-route composition is a wrapping `route_handler` (the adapter idiom: the wrapper runs its pre/post logic around the inner handler, which also covers per-route phase selectivity and ordering); server-wide hooks live on the `server::hooks()` bus alone. |
| The alias setters `internal_error_handler`, `auth_handler` and `log_access` install bus-backed handlers | README.md §Error propagation; src/httpserver/webserver_hooks.hpp alias callouts | — | inventory-only | TASK-118: no v3 aliases — the `handler_exception` chain covers the 500 path (v2 answered a throwing `internal_error_handler` with the hardcoded empty-body 500 via `run_internal_error_handler_safely`; v3 preserves that containment -- a throwing hook on the chain is treated as `pass()` and the bare empty-body 500 answers -- and a double-fault is likewise the hardcoded empty-body 500), auth guards own their failure mode (no engine-wide seat), and access logging maps to the `response_sent`/`request_completed` phases; a throwing hook is caught, treated as `pass()`, and a pre-commit phase's contained throw (request_received/route_resolved/before_handler) is surfaced through the `handler_exception` chain at the dispatcher (a commit-path phase's throw cannot replace the committed head; the chain fires at most once per request). |

## Basic authentication

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| Missing credentials answer 401 with `WWW-Authenticate: Basic realm="<realm>"`, empty body | README.md §Authentication; src/httpserver/create_webserver.hpp; src/httpserver/http_response.hpp `unauthorized(scheme, realm)` | auth_basic.tseq:no_credentials | pinned | |
| Valid Basic credentials admit the request (200) | README.md §Authentication; src/httpserver/create_webserver.hpp | auth_basic.tseq:valid_credentials | pinned | |
| Invalid credentials re-challenge 401 with the same header shape | README.md §Authentication; src/httpserver/create_webserver.hpp | auth_basic.tseq:invalid_credentials | pinned | |

## Digest authentication

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| Unauthenticated request answers 401 with an RFC 7616 §3.3 challenge: `realm`, `qop="auth"`, `algorithm=MD5`, server-generated `nonce`/`opaque` (volatile, `<*>`-masked), `charset=UTF-8` | docs/architecture/errors.md (Digest challenge format); TASK-062 digest_challenge contract | auth_digest.tseq:challenge_structure | pinned | |
| Full RFC 7616 round-trip against the server nonce admits the request (200) | README.md §Authentication; src/httpserver/http_request.hpp; docs/architecture/errors.md | auth_digest.tseq:roundtrip_digest (curl-mediated) | pinned | |

## Forms and multipart uploads

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `application/x-www-form-urlencoded` POST fields are parsed and visible to the resource | README.md §Request / Headers, arguments, cookies; src/httpserver/http_request.hpp; docs/architecture/features.md | forms.tseq:urlencoded_echo | pinned | TASK-116: the v3 surface is the per-route `forms::make_urlencoded_route` adapter (the `httpserver::forms` area, decode within cap identical to v2: first-`=` split, `+` as 0x20, strict `%HH`, arrival-order repeats, first-value lookup). Migration deltas: (1) a malformed `%HH` answers 400 (v2 passed the bytes through literally); (2) a body or field count past `urlencoded_limits` (defaults mirroring v2's GET-arg budgets) answers 413 BEFORE any unbounded storage (v2 truncated silently); (3) the per-route opt-in adapter replaces the global `post_process_enabled`/`put_processed_data_to_content`/`unescaper` knobs (a custom decode hook is not ported -- strict decoding is the point). |
| `multipart/form-data` fields are parsed (MHD post processor) and visible to the resource; raw multipart body sent as a single segment exercises the parser under segmentation | README.md §Request / Uploaded files; src/httpserver/http_request.hpp | forms.tseq:multipart_field | pinned | TASK-117: the v3 surface is the streaming `forms::part_sink` visitor (`httpserver/forms/multipart.hpp`, the decoder and drivers in v3core's `detail/forms_multipart*.cpp`). Within cap, decode matches v2's documented behavior: field parts land in `form_fields` with arrival-order repeats and first-value lookup, quoted `Content-Disposition` values with backslash escapes decode, the first `Content-Disposition` wins, and the adapter applies the same content-type gate (a non-matching type still drains under the cap and reaches the handler with empty fields). Migration deltas: (1) caller-provided part sinks replace the library-side in-memory file copy (v2 concatenated every upload into the flat arg map via `set_arg_flat` -- an unbounded-copy antipattern not ported; `temp_file_part_sink` ports the disk-upload behavior: random or sanitized basenames under a directory, pre-truncation of leftovers, partial-file removal); (2) the four `multipart_limits` budgets (raw bytes, parts, per-part bytes, header block; defaults 65536/64/65536/8192 mirroring v2's arg budgets) answer 413 BEFORE any unbounded storage where v2's effective default was unbounded (`content_size_limit = SIZE_MAX`); (3) strictness deltas answer 400 where MHD silently produced no parts: missing first/final boundary, a delimiter truncated at EOF, a header line without a colon or CRLF, a part without a usable Content-Disposition (no `name`), a boundary outside RFC 2046 bchars or longer than 256 chars; (4) the documented hooks are the sink callbacks themselves -- `on_part_abort` fires exactly once per begun-but-unended part on every non-ok terminal (disconnect, cancellation, rejection, sink failure) and `should_keep` is consulted exactly once per completed file at sink destruction (false/null/throwing removes, v2 `file_cleanup_callback` rules); the server-wide `request_completed`-on-abort wiring landed in TASK-118 (the v3 lifecycle bus fires the phase exactly once per exchange settle, disconnect included). |
| MHD's post processor tolerates LWSP transport padding of any length after a multipart delimiter while scanning for the terminating CRLF | RFC 2046 §5.1.1 (delimiter transport padding); TASK-117 streaming invariant (`forms/multipart.hpp`: the raw body is never buffered whole) | forms.tseq:multipart_field | pinned | TASK-117 validation: v3 bounds the tolerated delimiter padding to the boundary length plus 8 bytes; a longer run (terminated or not) is the typed `invalid_argument` 400 strictness delta recorded in the TASK-117 decoder matrix (at-bound padding decodes, one byte past is the sticky rejection, and an unterminated run over many 512-byte feeds is rejected during a feed). The bound keeps the decoder's pending window at O(boundary + feed) and the per-feed rescan linear against unterminated padding (CWE-400), where MHD's unbounded tolerance admitted an asymmetric memory+CPU drain up to the configured cap. |

## File responses

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `http_response::file` streams the file body with `application/octet-stream` and Content-Length framing | README.md §Response; src/httpserver/http_response.hpp `file()` contract; src/httpserver/http_response.hpp | file_resp.tseq:file_ok | pinned | TASK-120: native loopback replay through `response_definition::reopen_file` (actual file I/O, per-send Content-Length). |
| Empty file streams a 200 with `Content-Length: 0` | v2 wire behavior (empty body) | file_resp.tseq:file_empty | pinned | TASK-120: native real empty-file send, zero-length framing and reusable connection. |
| A missing file does not throw at construction; failure is observable at dispatch as the sanitized 500 body `Internal Server Error` (CWE-209 fix: no exception text) | src/httpserver/http_response.hpp `file()` contract; docs/architecture/errors.md | file_resp.tseq:file_missing | pinned | TASK-120: fixture adapter maps the typed pre-commit `send_definition` failure to the sanitized 500 value. The general send API retains its typed error; post-commit failures do not rewrite the response. |
| Pipe / borrowed iovec / deferred response bodies | src/httpserver/http_response.hpp `pipe()`, `iovec()`, `deferred()` contracts; README.md §Response | file_resp.tseq:pipe_body, iovec_body, deferred_body | pinned (TASK-120, recorded on v2) | Native `owned_pipe` and per-send `factory` sources replay the body and framing. v3 has no separate scatter/gather response factory: an application factory pulls each borrowed span under its owner lifetime, or `borrowed()` leases one contiguous span; the native leased-borrowed test replays the iovec pin. Deferred callbacks become fresh per-send pull producers; MHD position/sentinel arguments are replaced by `body_chunk`. Pipe handles transfer as `FILE*` with exactly-once close and one-shot send; replayable streams require a factory. |

## IP controls

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `default_policy(REJECT)` + `webserver::allow_ip("127.0.0.1")` admits allow-listed peers normally (200) | README.md §IP blocking and unblocking; src/httpserver/webserver.hpp | ip_controls.tseq:allow_listed_loopback | pinned | TASK-119: `server::peer_policy` (mode reject_all + allow list) seeded from `server_options::peer_policy()` at listen(); replayed byte-identical by ip_controls_corpus and live by native_http1_e2e (allow_listed_loopback_serves, v4 and v6 loopback). |
| `deny_ip` refusal wire shape (connection closed before any HTTP response vs. an HTTP error) is policy-callback timing dependent in v2 | README.md §IP blocking and unblocking; src/httpserver/webserver.hpp | — | MHD-artifact | TASK-119 pins ONE deterministic shape at both evaluation points: TCP connect succeeds, the server closes, ZERO application bytes, no HTTP error synthesized. Accept-time refusal fires only `accept_decision{accepted=false, reason}`; revalidation refusal fires only `request_completed{succeeded=false, end=peer_refused}`. Pinned by native_http1_e2e (reject_all_refuses_with_zero_bytes, runtime_deny_closes_next_request, ipv6_loopback_policy) and ip_controls_corpus (denied_peer_refusal_twin). |
| v2 evaluated the policy at accept only; established connections were never re-checked | src/detail/webserver_callbacks.cpp (policy_callback) | — | delta | TASK-119 improvement: the head of the dispatch pipeline revalidates the exchange's peer snapshot against the live policy before any phase fires — a runtime deny takes effect on the NEXT request of an established keep-alive connection (in-flight exchanges complete; idle connections are not proactively closed; drain closes all). |
| Pattern vocabulary: exact literals and wildcard octets; middle wildcards accepted (implementation accident) | src/httpserver/ip_representation.hpp | — | delta | TASK-119: `net::parse_pattern` accepts exact literals, trailing-`*` wildcards (IPv4 only), and CIDR `/n` over both families (new, bit-exact); middle wildcards are REJECTED with a typed invalid_argument (the v2 middle-wildcard behavior was an implementation accident). One prefix-bits representation; `::ffff:x.y.z.w` normalizes equal to the plain IPv4 literal. |
| v2 `deny_ip`/`allow_ip` threw std::invalid_argument on a bad pattern | src/httpserver/webserver.hpp | — | delta | TASK-119 (REQ-037): `peer_policy::deny/allow/remove_*` return typed `http::outcome` (invalid_argument); V12 validates every options entry pre-listen with the same grammar; remove of an absent pattern is a documented ok no-op. |
| v2 fired accept_decision with a string reason ("denied"/"not-on-allow-list") | src/detail/webserver_callbacks.cpp | — | delta | TASK-119: the reason is the typed `peer_refusal` enum (denied / not_on_allow_list); values preserved. An unspec peer snapshot never matches an entry: refused under reject_all, admitted under accept_all. XFF/trusted-proxy handling: v2 had none (grep-verified); v3 likewise — transport peer only, XFF identity is application-level. |

## TLS

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `use_ssl(true)` with the test certificate serves HTTPS round-trips (200, expected bodies) | README.md §TLS (HTTPS) options; src/httpserver/create_webserver.hpp; docs/architecture/features.md | tls.tseq:get_hello_tls, tls.tseq:smoke_tls (curl-mediated) | pinned | |
| Byte-level TLS record observation (handshake shape, record segmentation) | — | — | inventory-only | out of scope for the baseline harness by design; deferred to the v3 TLS engine (TASK-130..136, conformance TASK-146) |

## WebSocket

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| Cleartext upgrade handshake answers `101 Switching Protocols` with `Upgrade: websocket` and the RFC 6455 §4.2.2 `Sec-WebSocket-Accept` (pinned exactly for the fixed client key `dGhlIHNhbXBsZSBub25jZQ==`) | docs/architecture/features.md (WebSocket); RFC 6455 §4.2.2 | websocket.tseq:upgrade_handshake_fixed_key | pinned (skips on builds without libmicrohttpd_ws) | |
| Post-upgrade frame echo (text frames) | docs/architecture/features.md | — | inventory-only | TASK-121..123 own the native handshake/frame engine; TASK-128 owns conformance; only the handshake is pinned here |

## SHOUTcast

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `http_response::shoutCAST()` emits the `ICY 200 OK` wire status line (unobservable through curl) with normal framing and body | src/httpserver/http_response.hpp `shoutCAST()` / `with_status()` contract; test/parity/v2_fixture.cpp `build_shoutcast` | shoutcast.tseq:icy_status_line | pinned | TASK-120: `http::status::from_code(200).with_shoutcast()` carries a fixed marker through sync values, exchange, after_handler hooks and immutable definitions; native loopback replay pins ICY, normal framing/body and keepalive. No arbitrary token or MHD flag accepted. |
| The same server answers ordinary `HTTP/1.1` for non-ICY responses | v2 wire behavior | shoutcast.tseq:plain_get_still_http | pinned | TASK-120: an ordinary HTTP response follows ICY on the same connection; marker cannot leak. The native suite also pins ICY with HTTP/1.0, HEAD and simultaneous sends of one definition. Numeric status equality, code and categories ignore wire metadata; interim 100 is always HTTP/1.1. |

## Recorded v2 artifacts not pinned (rationale rows)

These were observed while recording (D4) and deliberately excluded from
the corpus:

1. **Client half-close after a request kills the exchange.** A raw
   client that performs `shutdown(SHUT_WR)` after sending a complete
   request receives no response: v2/libmicrohttpd treats the FIN as
   connection termination and closes. The harness therefore never
   half-closes mid-case, and v3 (TASK-105/107: keepalive rules) is
   free to define deterministic half-request/half-close semantics —
   recorded here so the change is explicit, not accidental.
   Status: MHD-artifact.
2. **Deny-list refusal wire shape.** Whether a denied peer observes
   connection refusal (no bytes) or an HTTP error depends on where the
   policy callback rejects; not stable enough to pin cross-version.
   Status: resolved by TASK-119 — v2 stays MHD-artifact; v3 defines and
   pins its own deterministic shape (zero application bytes at both
   evaluation points; see IP controls table).
3. **Server wall-clock `Date` header.** Emitted on every response;
   elided by the normalizer so no expectation can depend on timing.
   Status: normalized, never pinned.
4. **Digest nonce/opaque values.** Server-generated per challenge;
   masked `<*>`, structure pinned instead. Status: normalized.
5. **Indefinite server wait on a partial request head.** v2 behavior
   under a truncated request head depends on MHD connection timeouts
   and is not documented; unpinnable. Status: MHD-artifact. (TASK-107
   defines v3's partial-head semantics explicitly.)

## Harness usage (for TASK-097+ implementers)

```
# from the test build directory
./transcript_runner                 # run the whole corpus
./transcript_runner routing         # one file (substring filter)
./transcript_runner --list          # list transcript:case ids
./transcript_runner --record        # re-record sidecars for curation
```

Corpus runtime on the v2 baseline: well under 1 second (all profiles
bind port 0; cases are delimited by wire framing, no sleeps). Adding a
case: extend the `.tseq` file, run `--record`, curate, commit both the
expectation and an inventory row above.

## TASK-120 completion coverage map

Authority audit: `doc/libhttpserver.3` is a short entry-point manpage,
not the detailed API contract. The citations above now use the v2 public
headers and relevant README sections. `docs/architecture/features.md`
describes capabilities, but response ownership comes from
`src/httpserver/http_response.hpp`. These are v2 authority even while
native v3 headers coexist in the tree.

| Remaining documented HTTP/1 contract | Executable v3 evidence or explicit migration exception |
|---|---|
| SHOUTcast token, ordinary isolation, known length, keepalive | `native_http1_parity`: `shoutcast_then_plain_same_connection`, `immutable_icy_definition_concurrent_reuse`, `icy_http10_and_head_keep_framing`, `after_handler_can_select_fixed_icy_metadata`; `http_status`: `shoutcast_metadata_is_copied_and_compared`; `http1_response_framer`: `icy_final_does_not_change_interim_http` |
| File success / empty / sanitized missing-file dispatch | `native_http1_parity`: `file_transcripts_use_real_sources` replays all `file_resp.tseq` cases; `response_definition`: `file_send_missing_file_fails_pre_commit`, `file_send_reopens_per_send`, `file_send_truncated_mid_send_fails` |
| Pipe, borrowed iovec, deferred bytes and framing | `native_http1_parity`: `file_transcripts_use_real_sources` (v2-recorded pipe/iovec/deferred pins), `leased_borrowed_body_matches_v2_iovec_pin`; `response_sources`: `threaded_pipe_sends_single_winner`, `threaded_borrowed_sends_share_span`; `response_definition`: `threaded_factory_sends_run_concurrent_producers`. Scatter/gather adapter and callback-shape exceptions documented above. |
| HTTP/1.0 known-length keepalive, unknown-length close; HTTP/1.1 chunked | `native_http1_e2e`: `http11_two_gets_keepalive_chunked`, `http10_get_closes_delimited`, `http10_keepalive_requested_honored`; `http1_response_mode`: `length_ignores_method_and_version`, `close_delimited_on_10`, `engine_selected_chunked_on_11`, `http_10_keepalive_token_keeps_alive`; `native_http1_parity`: HTTP/1.0 ICY followed by another response |
| HEAD, empty/no-content/304, declared representation length | `routing_corpus`: `head_both_methods`; `native_http1_parity`: ICY HEAD; `http1_response_mode`: `head_applies_to_any_version`, `no_content_strips_framing_fields`, `not_modified_is_metadata_only`; response body writes are rejected for bodyless modes |
| Ordered repeated request/response fields and trailers | `http1_parser`: `split_matrix_repeated_fields`; `http1_body_decoder`: `chunked_decodes_payload_and_ordered_trailers`; `http1_response_framer`: `chunked_multiple_trailers_in_order`, `trailers_get_the_head_field_validation`; `response_definition`: `send_appends_overlay_headers_after_base`, `send_delivers_overlay_trailers`. Repeated fields remain ordered `http::fields`, replacing v2 maps; unsafe response fields now fail typed (existing strictness delta). |
| Ordered pipelining / persistent connections | `routing_corpus`: `pipelined_keepalive`; `native_http1_e2e`: `pipelined_gets_answered_in_order`; `native_http1_parity`: all six file/body cases plus ICY then ordinary on one live connection |
| 404 / 405 / 500 custom pages, hooks, error mapping | `hooks_corpus`: `custom_404`, `custom_405`, hook short-circuit/mutation; `request_lifecycle`: throwing/invalid/empty factory and exception-chain cases; `native_http1_parity`: sanitized file error adapter. TASK-118 phase/per-route/throwing-factory exceptions remain in the hooks table. |
| Routing families and capture args | `routing_corpus`, `server_routes_families`, `native_http1_e2e`; TASK-118 literal-first registration recommendation, regex/constraint application adapters remain explicit exceptions above |
| Basic/Digest auth, forms, multipart and IP policy | `basic_auth_corpus`, `digest_auth_corpus`, `forms_urlencoded_corpus`, `forms_multipart_corpus`, `ip_controls_corpus` plus real native HTTP/1 integration scenarios; TASK-116/117 bounds and TASK-119 refusal/revalidation/pattern exceptions remain unchanged |
| Raw query and query/form convenience arguments | `http1_parser`: `split_matrix_target_forms` preserves `request_head::raw_target`; form args use the dedicated forms adapters, path args use `exchange::path_args()`. Migration exception: the engine does not populate a v2 merged query/form/path map or offer `get_arg`; applications split raw target at `?` and opt into decoding/budgets (use `forms::decode_urlencoded` for query-style data). Raw query stays escaped, matching v2 `get_querystring()`. |
| Request cookies / response cookies | Migration exception: use `head_fields.first("cookie")` with the standalone `cookie::parse_cookie_header()` helper (`cookie_render` parser tests), and `cookie::to_set_cookie_header()` appended to response `http::fields` once per cookie. v3 does not add v2 `get_cookie`/`with_cookie` conveniences; serialization is ordinary ordered fields. |
| Request body and footers | `body_reader`, `http1_body_decoder`, `native_http1_e2e` collection/stream cases: bounded pull-based reads replace v2 `get_content`; decoded trailers are carried separately by body reader completion, replacing `get_footers`. Multipart disk sinks replace v2 uploaded-file map (TASK-117). |
| TLS and WebSocket | **Deferred**, not passing native TASK-120 parity: TLS TASK-130..136 / gate TASK-146; WebSocket TASK-121..123 / gate TASK-128. The v2 transcript runner still executes their baseline pins when supported. |

The half-close, partial-head timeout, volatile Date/auth values and MHD
refusal artifacts above remain deliberate exceptions. TASK-120 introduces
no SHOUTcast metadata/audio feature beyond the ICY response token. A
compact application migration example is in `http1-migration.md`.

Recording provenance, retained observations, passing replay results and the
nonzero record-mode accounting defect are documented in
[TASK-120-parity-evidence.md](TASK-120-parity-evidence.md).
