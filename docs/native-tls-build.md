# Native TLS provider build boundary

The native provider defaults to off (`--disable-v3-tls`). Enable it with
`--enable-v3-tls`. The selected provider must be stable OpenSSL **3.5.x**, with
patch **9 or later**. LibreSSL, prereleases, and other release lines are rejected.
The floor follows the [September 29, 2026 advisory](https://openssl-library.org/news/secadv/20260929.txt)
and [official downloads](https://openssl-library.org/source/); recheck advisories
before a release. This build gate requires the third-party QUIC TLS callback
interface, including its callback types, identifiers, and three linkable entry
points. It does not substitute the provider's native QUIC transport API.

Provider discovery uses `pkg-config openssl`, or an explicit selection:

```sh
../configure --enable-v3-tls \
  V3_TLS_CFLAGS='-I/path/to/openssl-3.5/include' \
  V3_TLS_LIBS='-L/path/to/openssl-3.5/lib -lssl -lcrypto'
```

An explicit selection never falls back to pkg-config. Selected include and link
flags stay with the private provider and dependent native targets. TLS-off does
not discover the provider or retain its flags. The provider smoke uses real
libssl and libcrypto, checks that runtime and header versions match, and creates
and frees a TLS context and session. Local configure checks runtime matching;
cross builds must run the smoke on the target in CI.

`<httpserver/features.hpp>` exposes the same compiled `query_features()` declaration
in both builds, without backend headers or configuration macros. Availability of
the provider is distinct from availability of TCP TLS, HTTP/2, and HTTP/3. TCP TLS is available in TLS-on builds for certificate, initial mTLS and eligible
external-PSK HTTP/1.0 and HTTP/1.1 in both managed and external modes. Certificate
and initial mTLS profiles also serve enabled HTTP/2 through TLS ALPN `h2`.
HTTP/3 public listeners remain unavailable. `listen()`
rejects unsupported protocol/profile requests before binding;
`server_options::validate()` remains semantic.

Run `make check-v3-native-linkage` in each mode. It inspects the executable behind
any libtool wrapper and the native archive, fails closed on missing artifacts or
inspectors, and permits libssl/libcrypto only in the enabled native lane. Run the
configure matrix with a genuine provider installed in an isolated prefix:

```sh
V3_TLS_TEST_PREFIX=/path/to/openssl-3.5 make check-v3-tls-build
```

The matrix changes genuine headers for bounded rejection fixtures; those fixtures
are never positive provider proof. The positive case uses the unmodified provider.
The installed aggregate contains native objects only. TLS-off has no MHD,
GnuTLS or OpenSSL dependency, probe or pkg-config requirement. TLS-on exposes
selected OpenSSL flags through `Libs.private` for static consumers while keeping
backend headers out of the installed API. Platform/thread libraries also appear
in `Libs.private`. Curl is a test tool and has no installed-package allowance.

Version 3.0.0 advances the SOVERSION to 3. Separate ABI-2 and ABI-3 executables
can use the versioned runtime files in one library directory. Development
headers, metadata, archives and the unversioned link keep shared names.

Strict installed auditing is the default and required in both TLS modes. It
checks configure/build provenance, installed metadata, effective public headers,
recursive dependency graphs and all static-archive members, including unused
members. Provider build proof remains distinct from encrypted listener or QUIC
interoperability proof. Nonlocal BSD, macOS and Windows execution belongs to CI
and the v3 PR; public H2/H3 listeners remain later-task work.

Run the clean installed-consumer matrix from a bootstrapped source checkout:

```sh
python3 scripts/check-v3-installed-consumer.py \
  --output /fresh/disposable/receipt-directory \
  --provider-prefix /path/to/openssl-3.5
```

Repeat `--prerequisite-prefix` for separate prerequisite library prefixes and
`--pkg-config-dir` for explicitly selected prerequisite metadata directories
(such as macOS SDK zlib metadata). The runner refuses an existing output path,
clears ambient consumer include/link flags, and isolates pkg-config discovery.
It independently builds and installs both modes, compiles shared and static
consumers, checks provider/TCP TLS availability and typed missing-credential outcomes,
serves bounded plaintext traffic, and compares installed header file sets and
effective declarations. `make check-v3-installed-consumer
INSTALL_AUDIT_ARGS='...'` invokes the same standalone matrix.

`make check-v3-installed-package-fixtures` runs adversarial policy tests; fixtures
are distinct from real package proof. The standalone auditor accepts
`--mode strict` for the post-cutover policy. Its JSON receipts retain artifact
hashes, resolved dependency edges, policy declarations, command results, header
include traces and effective-declaration hashes, and all-member archive symbols.
Preprocessor output is hashed rather than repeating the full standard library
text in every header receipt. Named macOS shared-cache platform libraries are
recorded as sealed OS leaves, without assuming all `/usr/lib` names are safe.
See [TASK-148 local receipts and limitations](task-148-installed-consumer-evidence.md).

TASK-130 establishes a private nonblocking TCP TLS adapter over owned raw I/O
operations. Its [local evidence](task-130-tls-io-evidence.md) covers the adapter
and independent loopback peers. TASK-193 composes this adapter and the existing credential registry into the
public native TCP listener.

## Installed four-platform package gate

`make check-v3-installed-consumer INSTALL_AUDIT_ARGS="..."` runs independent
TLS-off/on installs, shared/static public consumers in managed/external modes,
and applicable integration tools linked to the installed libraries. See
[the package evidence contract](task-182-package-validation-evidence.md) for
commands, the platform/backend matrix, private test-header provenance, current
public transport rejection behavior, and nonlocal CI ownership. The opt-in
runner requires a genuine selected shared OpenSSL provider satisfying configure;
CI prepares checksummed OpenSSL 3.5.9. Policy and orchestration fixture tests run
through `make check-v3-installed-package-fixtures`.


## Public certificate credentials and native TCP TLS

```cpp
#include <httpserver.hpp>
#include <memory>

// certificate_pem includes the leaf followed by its intermediate chain;
// private_key_pem contains the matching unencrypted PEM private key.
auto credentials = std::make_shared<httpserver::server::tls_credentials>();
httpserver::server::tls_credentials_config material;
material.hosts.push_back({"www.example.com", certificate_pem, private_key_pem,
                          {"http/1.1"}});
auto installed = credentials->replace(material);
if (!installed.ok()) { /* handle the typed outcome */ }
httpserver::server::server_options options;
options.tls().provider = httpserver::server::tls_provider::system_default;
options.tls().profile = httpserver::server::tls_profile::certificates;
options.add_listener({"127.0.0.1", 8443, true, credentials});
httpserver::server::native_server server(options);
// Register ordinary routes, then check server.listen().
```

`tls_credentials` copies share a store. Each successful `replace(config)` builds
and validates a complete immutable generation before atomic publication. Rejected
material (invalid host/default index, malformed PEM, mismatched key or unsupported
ALPN) leaves the previous publication active. The store retains provider objects,
not the PEM input. Caller buffers and handles may die after configuration; the
listener retains the store and accepted handshakes/sessions pin their generation
through retiring I/O completions. No global credential store exists.

Host names use exact canonical DNS matching, with case folding and a final dot
removed. Unknown or absent SNI selects `default_host` (zero by default). Each
listener has its own explicit store; sharing is intentional only when the same
store is assigned. Certificate and initial mTLS ALPN may contain `h2`,
`http/1.1`, or both; empty ALPN selects ordinary HTTP/1 fallback. An advertised
protocol must be enabled in every listener bound to the credential store. PSK
profiles retain HTTP/1-only ALPN and reject `h2`. Later
publication cannot negotiate a disabled ALPN; wire requests for a disabled H1
version are rejected before routing. `exchange::tls()` and request hook contexts
carry library-owned immutable authenticated peer metadata; plaintext uses null,
and anonymous certificate TLS has no client certificate identity.

Native admission reserves its connection budget before constructing TLS state.
Handshakes share the existing bounded provider lane and use an absolute
`timeouts().handshake` deadline. A stalled peer cannot reach an H1 reader or
handler. Drain cancels pending handshakes and stops new admission; established
H1 exchanges retain ordinary drain semantics. `request_stop()` remains safe from
handlers; call blocking `stop()` from the coordinator. External hosts must keep
advancing published readiness, wake and timer deadlines, including empty dispatch
when only a deadline is due (the same contract as plaintext).

Pure structural option errors take precedence in both loop modes. An unavailable
build/protocol/profile returns `not_supported`. TLS-on missing/unpublished
credentials or incompatible ALPN return `invalid_argument`. TLS-off exposes the
same declarations, returns `not_supported` from `replace` and rejects valid TLS
requests before binding. Every endpoint binds before admission activates; failure
on a later bind synchronously releases earlier endpoints and reports no bound
ports. A bind failure is terminal for that server instance.

TASK-193 supplies public certificate/default/SNI configuration and generation
lifetime safety. TASK-194 extends the same store to initial mTLS, HTTP/1 external
PSK and ACME, and exercises live listener rotation through installed packages.
Private profile suites remain supporting protocol evidence.

Run the installed certificate capability matrix after staging a package:

```sh
python3 scripts/check-v3-public-tls.py --prefix /installed/package \
  --provider-prefix /selected/openssl --build /identified/build \
  --tls yes --output /fresh/public-tls-receipts
```

The same source compiles in TLS-off and TLS-on lanes. The runner checks staged
public header origins, pkg-config identity, shared/static linkage and dependency
graphs; independent Python SSL clients verify chains, named hosts, SNI/default
certificates, ALPN, H1 status/version/body and lifecycle behavior in both loops.
Unknown/absent SNI checks verify the chain and compare the selected certificate
with its independently hostname-verified named certificate. TLS-off runs the same
configuration code and requires typed unavailability. Nonlocal platform checks
remain CI/v3-PR owned.

## Public initial client authentication, external PSK and ACME

`server/tls_credentials.hpp` supplies provider-neutral host trust, authentication,
owned keys and a bounded callback runtime. Set `tls_host_credentials::profile`
and the listener's `options.tls().profile` consistently. For mTLS, set
`trust_roots_pem` to the client CA roots. `mutual_tls` defaults to `require`;
explicit `client_auth.mode = tls_client_certificate_mode::request` verifies any
presented chain while allowing anonymous peers. `require` refuses absent or
untrusted client chains. Set the same resolved mode on the listener. Authentication
is initial-handshake only. Exchanges and request hooks own immutable verified
leaf identity, fingerprint and validity metadata.

A bound store rejects publication that contradicts any active listener's profile,
resolved mode or enabled HTTP/1 policy. Bind registration and publication share
one private writer transaction; a concurrent writer cannot downgrade authentication
between validation and admission. Compatible listeners hold independent leases;
stopping admission releases that listener's lease. Already accepted sessions keep
their pinned generation. Invalid material and policy leave the active generation
unchanged. Mixed profiles in one store cannot attach to a listener configured for
one contradictory profile.

For HTTP/1 external PSK, create `tls_psk_runtime` with finite `lookup_workers`
(1–64) and `lookup_queue` (0–65536), supply `tls_psk_config::lookup` and its shared
runtime, and set the host/listener profile to `external_psk`. Certificate/key/trust
material must be empty for PSK. Return `tls_psk_result` with an accepted status and
move-only `tls_psk_key` built from owned bytes. Identity/context references last
one invocation; retain copies when needed. TLS1.3 identities are binary; TLS1.2
exposes textual NUL-terminated identities. The context owns canonical selected
host, generation identity, absolute monotonic deadline and cancellation token.
Callbacks can run concurrently and must synchronize mutable captured state.

The lookup runtime is distinct from native server-owned provider workers. It
counts active and queued callbacks together; ignored cancellation retains a slot
until callback return, including across rotation. Late keys are wiped and never
route requests. `request_stop()` is nonblocking. Call it before
`wait_retired(absolute_deadline)` to observe actual callback retirement: timeout
means application work remains; waiting from that runtime's callback returns
`would_deadlock`. Destruction requests stop without joining arbitrary application
code. Captured resources must be owned through retirement. A native handler's
`request_stop()` or `begin_drain()` does not stop a shared application runtime;
one server stopping cannot disable another server using that runtime.

Normal certificate sessions have a host/context namespace. Same-generation
resumption may reuse authentication; replacements create fresh contexts, so an
old session cannot bypass the replacement or cross host/store identity. External
PSK authentication differs from ticket resumption: PSK profiles and ACME disable
tickets/session caching. Every ACME handshake sends its challenge certificate.
Application 0-RTT remains disabled; early requests never enter routes.

`tls_credentials::publish_acme(tls_acme_challenge)` validates a supplied challenge
certificate/key, canonical DNS host, 32-byte authorization digest and future
absolute system-clock expiry no later than certificate expiry. The certificate
must have exactly one matching DNS SAN and one critical ACME authorization digest
extension. `remove_acme(host)` is canonical and idempotent. Publication validation
is independent of listener port. Selection requires matching exact canonical SNI,
sole `acme-tls/1`, TCP, and the socket's actual local port443. Other ALPN offers and
ordinary HTTP use normal credentials; challenge sessions cannot route HTTP. This
API performs no certificate issuance or challenge-client orchestration.

TLS-off installs identical declarations and secure key operations, with typed
`not_supported` for credential/runtime/ACME availability. Public H2/H3 listeners
remain later-task work; external PSK remains HTTP/1-only. The installed rotation
example takes `certificate-directory managed|external port`, opens a real public
listener and accepts `replace`, `invalid` and `stop` on stdin. Its independent
installed witness holds a usable connection across rotation and validates the
new certificate plus rejected-publication preservation.

Public HTTP/2 uses the same native server, route table, hooks, peer policy and
exchange API as HTTP/1. See [the public example](../examples/v3_http2.cpp) and
[TASK-195 design](../specs/architecture/v3/TASK-195-design.md). TLS-off
configuration rejects HTTP/2 before binding. TLS 1.2 HTTP/2 rejects forbidden
ciphers before publishing request hooks.

Connection wire operations run serially; each stream has its own handler
affinity on the configured workers. A running handler can occupy one worker,
so applications should keep handlers cooperative and size the worker pool for
their concurrent work. Receive credit follows body consumption. Configured
server limits bound listener, connection and stream descendants, including
header copies, body rings, output and WebSocket storage. Header, SETTINGS,
body-idle, suspension, write-idle, WebSocket close and drain timers use native
readiness deadlines without periodic polling.

RFC8441 Extended CONNECT upgrades one stream and permits ordinary siblings.
Native drain publishes staged GOAWAY, finishes accepted work, and cancels
remaining requests and WebSockets at its absolute deadline. A retained ticket
keeps the original deadline result; its remaining count describes unfinished
work at expiry, even after actual retirement. A counted handler cannot wait
on its own drain ticket.
