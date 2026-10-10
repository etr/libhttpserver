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
the provider is distinct from availability of TCP TLS, HTTP/2, and HTTP/3. These
transports remain unavailable until their owning tasks implement them. Native
`listen()` returns `not_supported` before binding any endpoint for a configuration
requesting those transports; `server_options::validate()` remains semantic.

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
and the v3 PR; public TLS/H2/H3 listeners and credential installation remain gaps.

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
consumers, checks provider availability and the typed TLS-listener rejection,
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
and independent loopback peers. Public TCP TLS availability remains false until
listeners have a usable credential/configuration path.

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
