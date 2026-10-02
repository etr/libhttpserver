# v2 observable-behavior parity inventory (TASK-096)

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
- **Documentation source** — where v2 documents it (doc/libhttpserver.3,
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
| Exact-path GET returns 200 `text/plain` body with Content-Length framing, connection stays open | doc/libhttpserver.3 (webserver, register_path); docs/architecture/features.md | routing.tseq:get_hello | pinned | |
| Parameterized exact paths (`/params/{id}/name/{name}`) bind captures as request args | docs/architecture/features.md (routing tiers); src/httpserver/webserver_routes.hpp contract | routing.tseq:parameterized_path | pinned | |
| `method_set` GET+HEAD serves HEAD as a headers-only response; the GET body length is still declared via `Content-Length` and no body bytes follow (RFC 7231 §4.3.2) | RFC 7231 §4.3.2; v2 wire behavior | routing.tseq:head_both_methods | pinned | TASK-105: HEAD/no-content rules |
| Unregistered method on a registered path answers 405 with `Allow: <methods>` and the fixed body `Method not Allowed` | docs/architecture/errors.md (405 handling) | routing.tseq:method_not_allowed | pinned | |
| Miss path answers 404 `text/plain` body `Not Found` | docs/architecture/errors.md | routing.tseq:not_found | pinned | |
| Two pipelined requests on one connection are answered in order; connection survives | docs/architecture/threading.md (connection handling) | routing.tseq:pipelined_keepalive | pinned | TASK-107: pipelined response order |

## Hooks

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `after_handler` hook mutation (`with_header`) lands on the wire | docs/architecture/hooks.md (after_handler) | hooks.tseq:get_hello_hook_header | pinned | |
| `before_handler` short-circuit answers its response and skips the resource handler | docs/architecture/hooks.md (before_handler short-circuit) | hooks.tseq:before_handler_403 | pinned | |
| A short-circuiting `before_handler` also suppresses `after_handler` (no hook header on the 403) | docs/architecture/hooks.md (phase order) | hooks.tseq:before_handler_403 (`expect header ~X-Hook`) | pinned | |
| `not_found_handler` supplies the 404 body; the handler owns the status code (v2 does not override it) | docs/architecture/hooks.md (not_found alias); v2 wire behavior | hooks.tseq:custom_404 | pinned | |
| `method_not_allowed_handler` supplies the 405 body; handler owns the status; `Allow` still emitted | docs/architecture/hooks.md (method_not_allowed alias) | hooks.tseq:custom_405 | pinned | |

## Basic authentication

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| Missing credentials answer 401 with `WWW-Authenticate: Basic realm="<realm>"`, empty body | doc/libhttpserver.3 (basic auth); src/httpserver/http_response.hpp `unauthorized(scheme, realm)` | auth_basic.tseq:no_credentials | pinned | |
| Valid Basic credentials admit the request (200) | doc/libhttpserver.3 (basic auth) | auth_basic.tseq:valid_credentials | pinned | |
| Invalid credentials re-challenge 401 with the same header shape | doc/libhttpserver.3 (basic auth) | auth_basic.tseq:invalid_credentials | pinned | |

## Digest authentication

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| Unauthenticated request answers 401 with an RFC 7616 §3.3 challenge: `realm`, `qop="auth"`, `algorithm=MD5`, server-generated `nonce`/`opaque` (volatile, `<*>`-masked), `charset=UTF-8` | docs/architecture/errors.md (Digest challenge format); TASK-062 digest_challenge contract | auth_digest.tseq:challenge_structure | pinned | |
| Full RFC 7616 round-trip against the server nonce admits the request (200) | doc/libhttpserver.3 (digest auth); docs/architecture/errors.md | auth_digest.tseq:roundtrip_digest (curl-mediated) | pinned | |

## Forms and multipart uploads

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `application/x-www-form-urlencoded` POST fields are parsed and visible to the resource | doc/libhttpserver.3 (POST args); docs/architecture/features.md | forms.tseq:urlencoded_echo | pinned | TASK-116: the v3 surface is the per-route `forms::make_urlencoded_route` adapter (the `httpserver::forms` area, decode within cap identical to v2: first-`=` split, `+` as 0x20, strict `%HH`, arrival-order repeats, first-value lookup). Migration deltas: (1) a malformed `%HH` answers 400 (v2 passed the bytes through literally); (2) a body or field count past `urlencoded_limits` (defaults mirroring v2's GET-arg budgets) answers 413 BEFORE any unbounded storage (v2 truncated silently); (3) the per-route opt-in adapter replaces the global `post_process_enabled`/`put_processed_data_to_content`/`unescaper` knobs (a custom decode hook is not ported -- strict decoding is the point). |
| `multipart/form-data` fields are parsed (MHD post processor) and visible to the resource; raw multipart body sent as a single segment exercises the parser under segmentation | doc/libhttpserver.3 (file upload / post processor) | forms.tseq:multipart_field | pinned | TASK-117: the v3 surface is the streaming `forms::part_sink` visitor (`httpserver/forms/multipart.hpp`, the decoder and drivers in v3core's `detail/forms_multipart*.cpp`). Within cap, decode matches v2's documented behavior: field parts land in `form_fields` with arrival-order repeats and first-value lookup, quoted `Content-Disposition` values with backslash escapes decode, the first `Content-Disposition` wins, and the adapter applies the same content-type gate (a non-matching type still drains under the cap and reaches the handler with empty fields). Migration deltas: (1) caller-provided part sinks replace the library-side in-memory file copy (v2 concatenated every upload into the flat arg map via `set_arg_flat` -- an unbounded-copy antipattern not ported; `temp_file_part_sink` ports the disk-upload behavior: random or sanitized basenames under a directory, pre-truncation of leftovers, partial-file removal); (2) the four `multipart_limits` budgets (raw bytes, parts, per-part bytes, header block; defaults 65536/64/65536/8192 mirroring v2's arg budgets) answer 413 BEFORE any unbounded storage where v2's effective default was unbounded (`content_size_limit = SIZE_MAX`); (3) strictness deltas answer 400 where MHD silently produced no parts: missing first/final boundary, a delimiter truncated at EOF, a header line without a colon or CRLF, a part without a usable Content-Disposition (no `name`), a boundary outside RFC 2046 bchars or longer than 256 chars; (4) the documented hooks are the sink callbacks themselves -- `on_part_abort` fires exactly once per begun-but-unended part on every non-ok terminal (disconnect, cancellation, rejection, sink failure) and `should_keep` is consulted exactly once per completed file at sink destruction (false/null/throwing removes, v2 `file_cleanup_callback` rules); the server-wide `request_completed`-on-abort wiring is TASK-118 (the v3 lifecycle bus has no per-request hooks yet). |

## File responses

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `http_response::file` streams the file body with `application/octet-stream` and Content-Length framing | doc/libhttpserver.3 (file_response); src/httpserver/http_response.hpp | file_resp.tseq:file_ok | pinned | |
| Empty file streams a 200 with `Content-Length: 0` | v2 wire behavior (empty body) | file_resp.tseq:file_empty | pinned | |
| A missing file does not throw at construction; failure is observable at dispatch as the sanitized 500 body `Internal Server Error` (CWE-209 fix: no exception text) | src/httpserver/http_response.hpp `file()` contract; docs/architecture/errors.md | file_resp.tseq:file_missing | pinned | |
| Pipe / iovec / deferred response bodies | docs/architecture/features.md | — | inventory-only | covered by unit-level factories tests (`http_response_factories`); transcripts deferred to the v3 engine tasks that re-home those body kinds |

## IP controls

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `default_policy(REJECT)` + `webserver::allow_ip("127.0.0.1")` admits allow-listed peers normally (200) | doc/libhttpserver.3 (IP access control) | ip_controls.tseq:allow_listed_loopback | pinned | |
| `deny_ip` refusal wire shape (connection closed before any HTTP response vs. an HTTP error) is policy-callback timing dependent in v2 | doc/libhttpserver.3 (IP access control) | — | MHD-artifact | v3 must define and document one deterministic refusal shape; TASK-107+ pin it against the v3 contract, not the v2 shape |

## TLS

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `use_ssl(true)` with the test certificate serves HTTPS round-trips (200, expected bodies) | doc/libhttpserver.3 (TLS); docs/architecture/features.md | tls.tseq:get_hello_tls, tls.tseq:smoke_tls (curl-mediated) | pinned | |
| Byte-level TLS record observation (handshake shape, record segmentation) | — | — | inventory-only | out of scope for the baseline harness by design; lifted when the v3 TLS engine lands (TASK-110+) |

## WebSocket

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| Cleartext upgrade handshake answers `101 Switching Protocols` with `Upgrade: websocket` and the RFC 6455 §4.2.2 `Sec-WebSocket-Accept` (pinned exactly for the fixed client key `dGhlIHNhbXBsZSBub25jZQ==`) | docs/architecture/features.md (WebSocket); RFC 6455 §4.2.2 | websocket.tseq:upgrade_handshake_fixed_key | pinned (skips on builds without libmicrohttpd_ws) | |
| Post-upgrade frame echo (text frames) | docs/architecture/features.md | — | inventory-only | TASK-107+ own protocol-level (frame) transcripts; only the handshake is pinned here |

## SHOUTcast

| Documented v2 behavior | Documentation source | Transcript case | Baseline status | Migration note |
|---|---|---|---|---|
| `http_response::shoutCAST()` emits the `ICY 200 OK` wire status line (unobservable through curl) with normal framing and body | doc/libhttpserver.3 (shoutCAST); src/httpserver/http_response.hpp | shoutcast.tseq:icy_status_line | pinned | |
| The same server answers ordinary `HTTP/1.1` for non-ICY responses | v2 wire behavior | shoutcast.tseq:plain_get_still_http | pinned | |

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
   Status: MHD-artifact (see IP controls table).
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
