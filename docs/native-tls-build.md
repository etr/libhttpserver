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
the provider is distinct from availability of TCP TLS, HTTP/2, and HTTP/3. TCP TLS is available in TLS-on builds for certificate-authenticated HTTP/1.0
and HTTP/1.1 in both managed and external modes. HTTP/2 and HTTP/3 remain
unavailable until their owning tasks implement public listeners. `listen()`
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
and the v3 PR; public H2/H3 listeners and expanded TLS profiles remain later-task work.

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
store is assigned. ALPN may contain `http/1.1` or be empty for ordinary HTTP/1
fallback. A listener with HTTP/1.1 disabled must initially use empty ALPN. Later
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

TASK-193 supports public certificate/default/SNI configuration and replacement
lifetime safety. Public mTLS, external PSK and ACME operations remain TASK-194
work; native requests for unsupported profiles reject honestly. Private profile
suites continue to test their existing support. The rotation example now uses the
public store and demonstrates typed publication outcomes; expanded installed
profile/rotation examples belong to TASK-194.

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
