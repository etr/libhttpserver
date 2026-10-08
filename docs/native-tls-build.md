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
`CPPFLAGS` and `LDFLAGS`, if needed for legacy prerequisites, must be exported when
running the matrix.

The transitional installed aggregate library and pkg-config file still include
declared legacy MHD/GnuTLS dependencies and their provider support libraries.
Curl belongs to the test link path and has no installed-package allowance. TASK-184 owns their removal and package/ABI
cutover; this task establishes the native convenience archive boundary. Provider
build proof is not TCP TLS or QUIC interoperability proof. Nonlocal BSD and
Windows checks belong to CI and the v3 PR.

[TASK-148](../specs/tasks/M10-tls-http2/TASK-148.md) audits this transitional
installed package, requiring declared and explained legacy dependencies while
keeping native-only artifacts and installed public headers backend-free. It also
provides a strict post-cutover policy that rejects legacy linkage. Transitional
audit success is not final v3 dependency compliance; TASK-184 and TASK-185 retain
the package cutover and final release gates.

Run the clean installed-consumer matrix from a bootstrapped source checkout:

```sh
python3 scripts/check-v3-installed-consumer.py \
  --output /fresh/disposable/receipt-directory \
  --provider-prefix /path/to/openssl-3.5 \
  --prerequisite-prefix /path/to/legacy-prerequisites
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
