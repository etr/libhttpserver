# Native protocol capacities

Include `<httpserver/server/protocol_limits.hpp>` directly, or use
`<httpserver/server/configuration.hpp>`. `server_options::http2()`, `http3()`
and `quic()` each return mutable/const references to an optional value profile.
A missing profile keeps the existing native derivation. A present profile,
including an empty one, requests explicit capacity validation. Individual unset
members retain their defaults.

```cpp
httpserver::server::server_options options;
// Configure TLS, credentials, enabled protocols and matching listeners first.
options.budgets().set(httpserver::server::resource::header_fields, 4096);
options.budgets().set(httpserver::server::resource::quic_reassembly_bytes,
                      16 * 1024 * 1024);
options.http2().emplace().max_concurrent_requests = 192;
options.http2()->decoder_table_bytes = 8192;
options.http2()->encoder_table_bytes = 16384;
options.http3().emplace().max_concurrent_requests = 24;
options.http3()->max_blocked_headers = 32;
options.quic().emplace().max_stream_records = 96;
httpserver::server::effective_protocol_limits limits;
auto result = options.resolve_limits(limits);
if (result.ok()) {
    httpserver::server::native_server server(options);
    result = server.effective_limits(limits);
}
```

`header_capacity_options` holds `max_compressed_bytes`, `max_expanded_bytes`
and `max_fields`, all optional `std::size_t` values. H2/H3 profiles contain
`headers`, `max_concurrent_requests`, `decoder_table_bytes` and
`encoder_table_bytes`. H3 additionally contains `max_blocked_headers` and
`max_retained_header_bytes`. QUIC contains `max_stream_records`.

`resolve_limits(effective_protocol_limits&) const` performs semantic and storage
planning, then checks compiled protocol/provider availability. It performs no I/O,
credential acquisition, reservations, environment reads, binds or worker startup.
`validate()` runs the same semantic/storage planner without the build availability
gate; a valid TLS configuration can therefore validate in a TLS-off build while
resolution returns `not_supported`. Startup completes those checks before binding
any endpoint, then checks credentials/backend readiness.

`native_server::effective_limits(effective_protocol_limits&) const` reads the
immutable plan resolved from the server's copied options. It works before listen,
while serving or draining, after a failed listen, and after stop. Credential
publication/replacement and mutations of the original options cannot change that
report. Both methods leave the entire output unchanged on failure. Inspection is
local startup policy; it does not inspect live connections or imply a successful
handshake. Managed and external loops use the same plan.

H2 applies to enabled H2 with a TLS TCP listener. H3 and QUIC apply to enabled H3
with an explicit TLS UDP listener. Inapplicable effective sections are absent.
A present profile for an inapplicable/disabled protocol fails `invalid_argument`.
Unsupported compiled transports fail build-aware resolution/startup with
`not_supported`. An unused TLS provider selection leaves plaintext HTTP/1 available. Existing TLS profile/transport rules still apply.

## Defaults and bounds

| Capacity | Unset native policy |
| --- | --- |
| H2 concurrent requests | `min(128, streams budget)` |
| H2 compressed header bytes | `min(65536, body_buffer_bytes / 16)` |
| H2 expanded header bytes | `min(65536, header_bytes / 3)` |
| H2 fields | `min(256, header_fields budget)` |
| H2 decoder/encoder table bytes | 4096 each |
| H3 concurrent requests | `min(16, streams budget)` |
| H3 compressed/expanded bytes/fields | 8192 / 16384 / 128 |
| H3 decoder/encoder table bytes | `min(4096, qpack_table_bytes budget)` each |
| H3 blocked headers | `min(8, blocked_headers budget)` |
| H3 retained compressed HEADERS bytes | 65536 |
| QUIC retained stream records | 64 |
| Initial QUIC bidi/uni stream credit | H3 request ceiling / 8 |
| Advertised/native send datagram bytes | 1200 / 1200 |

Positive explicit request ceilings must fit the aggregate stream budget. H2's
ceiling is 1,048,576; the historical128 is a default. H3 requests must also fit the
retained ledger alongside three local and three peer control/QPACK roles. The
ledger range is7..65,536; its actual prepaid storage can impose a smaller limit.
Explicit H3 policy needs at least four aggregate stream seats for three peer
critical roles and one request. Uni credit8 also permits unknown stream types;
those records compete with requests and terminal records for the same ledger.

Header bytes and fields must be positive. H2 expanded bytes fit three copies in
`header_bytes`; H3 needs five aggregate header-byte charges and an additional
semantic field seat (`max_fields + 1`). Field resource maxima are65,536 for H2
and65,535 for H3, subject to the configured budget. H2 compressed bytes fit the
body-buffer budget after native owner, frame and one stream descriptor workspace. H3 compressed/decoded metadata, retained HEADERS, semantic
copies, body/response buffers and descriptor arrays must fit its private prepaid
workspace. At least one maximum compressed section must fit retained HEADERS.
All arithmetic and wire conversions are checked; errors identify the field or
storage/budget relationship. Explicit values are rejected instead of clamped.

Compression tables permit zero, with a resource maximum16,777,216 bytes. Both
local directions must fit the same aggregate compression budget for an explicit
policy. H2 includes `max(4096, decoder_table_bytes) + encoder_table_bytes`, because
a reduced advertised decoder maximum becomes enforceable after SETTINGS ACK.
The initial4096 allowance is reported separately as
`effective_http2_limits::pre_ack_decoder_table_bytes`. Zero H2 encoder capacity
emits the required table-size update. See [RFC9113 §4.3.1](https://www.rfc-editor.org/rfc/rfc9113.html#section-4.3.1).

H3 uses `decoder_table_bytes + encoder_table_bytes`. Decoder zero with a positive
encoder reserves its output workspace; both zero still support static/literal
sections without dynamic inserts. Blocked-header ceilings permit zero and are
bounded by the aggregate blocked-header budget (maximum65,536). They are
independent of request/record ceilings. Encoder ceilings in both protocols are
local limits: actual use is additionally bounded by the peer's SETTINGS. See
[RFC9204 §§3.2.3,5,7.3](https://www.rfc-editor.org/rfc/rfc9204.html).

Private H3 data/critical subdivisions grow for explicit policy when the root
reassembly budget permits. Planning includes the retained flow ledger, native
transport/framing descriptors, bridge records and scratch/receipt/information
arrays, decoded fields, semantic copies, compression instruction/output work,
recovery metadata/payload, queued datagrams, TLS/CRYPTO and critical roles. The
UDP listener's131072-byte charge remains outside that prepaid envelope. Runtime
subdivision reservations stop at the prepaid boundary; semantic resources still
reach the shared ancestors. Completely unset policy preserves its old subdivision
`min(393216, floor(quic_reassembly_bytes * 3 / 8))` for each pool and retains its
existing runtime pressure behavior.

A successful resolution proves a bounded structural envelope and space for one
maximum request alongside persistent records and critical state. It does not
reserve all concurrent requests at their largest allowed sizes. Connections and
streams still compete for shared budgets and can encounter ordinary resource
refusal without changing the report.

## Enforcement, advertisement and retention

The report includes aggregate `budgets`, optional `http2`, `http3` and `quic`
sections, and nested `initial_advertisement` values. H2 advertises the resolved
request/decoder ceilings; H3 advertises its decoder/blocked ceilings and QUIC
advertises initial request credit. Neither protocol advertises a header-list or
field-section byte maximum. `max_header_list_bytes` and
`max_field_section_bytes` remain absent even with explicit local header limits.
Expanded size includes name/value bytes and32 bytes per field, as described in
[RFC9114 §4.2.2](https://www.rfc-editor.org/rfc/rfc9114.html#section-4.2.2).

Normal request retirement replenishes QUIC MAX_STREAMS credit. Initial credit is
not a lifetime request count. Terminal descriptors remain retained to prevent
stream recreation; eventually the total record ledger is authoritative. Larger
ledgers support more retirements while keeping this bound. See
[RFC9000 §§4.6,18.2](https://www.rfc-editor.org/rfc/rfc9000.html).

The inspected QUIC datagram values are fixed read-only implementation policy.
There is no datagram-size setter, PMTU growth or new key-update behavior here.
Existing per-key usage guards remain in force; this API makes no promise about
graceful connection-local key exhaustion.
