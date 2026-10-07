# TASK-132 early SNI and ALPN selection

Server contexts install ClientHello, SNI acknowledgement and ALPN callbacks
before publication. Each SSL owns callback state and one pinned credential
snapshot. ClientHello selection starts with that snapshot's explicit default,
parses the current SNI extension, and uses the same canonical ASCII DNS validator
as credential configuration. Matching is exact after case folding and removal
of one trailing dot. No wildcard or suffix lookup occurs. Malformed lengths,
duplicate host-name entries, embedded NUL and invalid DNS labels fail.
The later SNI callback only acknowledges a name accepted from this ClientHello;
it never reads a cached session name or reselects the host.

The selected context is installed before resumption lookup. Every server
context has a fresh, OpenSSL-generated 32-byte session-ID namespace; the
selected namespace is also installed on the SSL. The initial context still owns
ticket/cache machinery, so context switching alone is not the isolation policy.
Namespaces distinguish hosts and replacement generations, while the connection
keeps the snapshot alive throughout its callbacks and teardown. No callback
reacquires the registry or mutates a published context. Callback failures remain
provider alerts or redacted library failures; C++ exceptions cannot cross the
provider boundary.

ALPN follows the selected host's configured order, choosing only `h2` or
`http/1.1`. A nonempty policy with no supported offered overlap sends a fatal
`no_application_protocol` alert. Empty configuration disables ALPN negotiation.
If the client omits ALPN, HTTP/1-capable or empty policies allow legacy HTTP/1;
policies without HTTP/1 reject. Generic stored tokens remain valid configuration,
but `h3`, `acme-tls/1` and arbitrary tokens cannot be selected by this TCP HTTP
selector. Output references the provider's offered protocol bytes during the
handshake. Snapshot-less single-context sessions retain their existing usable
handshake path. Early application data remains disabled.

## Behavioral evidence

The worktree began clean on `task/TASK-132` at
`7037f051e209cc5bdb4124b10de0a7fbe024bf5b`, matching base `v3`. A pristine
focused rotation baseline passed **2 tests / 27 checks**; no full-suite pristine
baseline is claimed.

The independent peer uses explicit root verification and TLS 1.2/1.3 version
bounds, fragmented memory-BIO transport (73-byte server output and 41-byte
client output), and bounded handshake/ticket driving. It exercises the actual
native `tls_session`; adapter ownership/application-I/O coverage also remains in
the extended rotation suite. Certificates are observed as leaf serials 101/202.
SNI acknowledgement is observed in received ServerHello/EncryptedExtensions
wire messages, rather than through cached server-name accessors.

TDD receipts, before their corresponding production changes:

- SNI selection/acknowledgement and DNS rejection: **22 failing checks** across
  2 tests; GREEN was **2 tests / 62 checks**.
- Selected-profile ALPN: **26 failing checks**; GREEN was **3 tests / 122 checks**.
- Real ticket host isolation: **7 failing checks** before namespaces;
  GREEN was **4 tests / 176 checks**.
- The final suite is **6 tests / 260 checks**. A task-local regression probe
  compiling the actual implementation with context/SSL namespace isolation
  removed failed **17 checks** against that final suite. The production tree
  remained unchanged during this probe.

The final ticket tests require resumable, ticket-bearing sessions and
`SSL_session_reused()` on same-host/same-generation connections for both TLS
versions. Post-handshake TLS 1.3 tickets are consumed before capture. Renewed,
unused tickets are supplied to subsequent probes because TLS 1.3 tickets can be
single-use. Changed offers are renegotiated from the current ClientHello.
Fresh tickets presented with another selected host, unknown SNI or omitted SNI
perform full handshakes and expose that host's certificate/ALPN. Unknown/omitted
SNI can resume a default-host ticket without acknowledgement. Replacement
rejects old-generation tickets. TLS 1.2 successful resumption does not repeat
SNI acknowledgement; TLS 1.3 does acknowledge accepted current SNI.
Ticket metadata confirms a zero early-data allowance.

Two handshake threads (one per TLS version) and a publisher execute eight
barrier-bounded rounds. Each reader captures a delayed selection before the
first publication, then handshakes it and a fresh acquisition. A second
publication runs concurrently with those observations. Certificate/ALPN pairs
are correlated with the captured immutable metadata, and invalid replacements
preserve the exact active snapshot. Sixteen successful replacements finish at
generation 17. Extended adapter rotation tests verify different old/new ALPN
policies, established application I/O, failed-replacement retention, and weak
snapshot lifetime through adapter teardown and retiring executor work.

## Local acceptance

macOS ARM64, Apple Clang, C++20, Autotools VPATH builds. Provider readback:
`/private/tmp/task129-provider/install/bin/openssl version` reported
**OpenSSL 3.5.9 29 Sep 2026**. Bootstrap was `./bootstrap`.
TLS-on configuration:

```sh
mkdir -p build-on build-off
cd build-on
../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
```

TLS-off uses the same prerequisite/C++20 flags, `--disable-v3-tls`, and no
`V3_TLS_*` flags. Both complete builds passed. Focused execution commands from
the worktree root (explicit `check_PROGRAMS` keeps Automake's test prerequisite
build limited to the already selected executables):

```sh
make -C build-on -j2
make -C build-on/test -j2 tls_selection tls_credentials \
  tls_credentials_rotation tls_io tls_io_race tls_io_loopback io_tls_control
make -C build-on/test -j1 check-TESTS \
  check_PROGRAMS='tls_selection tls_credentials tls_credentials_rotation tls_io tls_io_race tls_io_loopback io_tls_control' \
  TESTS='tls_selection tls_credentials tls_credentials_rotation tls_io tls_io_race tls_io_loopback io_tls_control'
make -C build-on check-v3-native-linkage check-local
make -C build-off -j2
make -C build-off/test -j2 consumer_v3_features io_tls_control \
  io_backend_contract server_options_validate
make -C build-off/test -j1 check-TESTS \
  check_PROGRAMS='consumer_v3_features io_tls_control io_backend_contract server_options_validate' \
  TESTS='consumer_v3_features io_tls_control io_backend_contract server_options_validate'
make -C build-off check-v3-native-linkage check-local
git diff --check
```

Results:

- TLS-on: **7/7 executables**, **35 tests / 883 checks**, no failures/skips.
- TLS-off: **4/4 executables**, the feature consumer plus
  **62 tests / 920 checks**, no failures/skips.
- Native linkage audits and repository `check-local` passed in both modes.
- ASan/UBSan and TSan each passed **12 tests / 441 checks** across selection,
  credentials and rotation, with no sanitizer diagnostics.
- Changed-file cpplint, changed-source CCN <= 10, repository file-size, and
  `git diff --check` passed. Repository complexity still reports three unchanged
  violations: `dispatch_request` (11), `valid_peer_pattern` (15), and
  `pollsys::accept_one` (11). Fresh files from `git show v3:<path>` reproduced
  those same three findings; no new violation was introduced.
- Existing macOS libtool linker deprecation/duplicate-library warnings remain.

Initial restricted-sandbox loopback tests could not open listeners and are not
positive acceptance evidence. The complete focused sets above passed with
permission to bind ephemeral local sockets. Initial unrestricted Automake
`check-TESTS` prerequisite compilation was stopped and replaced by the bounded
explicit-`check_PROGRAMS` commands. An intermediate sanitizer run picked up an
unfinished test revision and failed test assertions; the final frozen-source
run supersedes it and passed both sanitizers.

## Sanitizer reproduction and limits

The task-local `/private/tmp/task132-sanitizers.py` compiled every affected native
source independently with each sanitizer, without an unsanitized native archive.
Equivalent commands from this worktree:

```sh
sh <<'SH'
provider=/private/tmp/task129-provider/install
sources='io_operation io_connection_owner fake_io_backend worker_pool tls_credentials tls_session tls_io_backend'
for sanitizer in address,undefined thread; do
  probe="/private/tmp/task132-reproduce-$sanitizer"
  mkdir -p "$probe"
  for source in $sources; do
    c++ -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
      -fsanitize="$sanitizer" -DHTTPSERVER_COMPILATION \
      -Isrc -Itest -I"$provider/include" \
      -c "src/detail/$source.cpp" -o "$probe/$source.o"
  done
  for target in tls_selection tls_credentials_rotation tls_credentials; do
    c++ -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
      -fsanitize="$sanitizer" -DHTTPSERVER_COMPILATION \
      -Isrc -Itest -I"$provider/include" -DTLS_TEST_DIR="\"$PWD/test\"" \
      "test/unit/${target}_test.cpp" "$probe"/*.o \
      -L"$provider/lib" -lssl -lcrypto -o "$probe/$target"
    "$probe/$target"
  done
done
SH
```

The selected OpenSSL static libraries are not sanitizer-instrumented. Linux,
BSD, Windows and other unavailable platform checks are unexecuted and belong to
CI/the v3 PR under AGENTS.md. No cloud/deployment/production proof is claimed.
Native TLS listener rejection and `query_features().tcp_tls == false` remain.
Negotiating `h2` does not serve HTTP/2 frames; mTLS enforcement, PSK, ACME and
QUIC remain downstream work. Groundwork validation, runner commits, integration
into `v3`, and worktree cleanup remain caller-owned.
