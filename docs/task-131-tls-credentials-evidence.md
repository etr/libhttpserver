# TASK-131 immutable TLS credential generations

The private `tls_credentials_registry` constructs every host context before
publication. It owns canonical DNS names, profile metadata, and validated ALPN
wire bytes, while provider-owned contexts own parsed certificates, intermediate
chains, keys, and explicit trust stores. Published snapshots retain no plaintext
PEM inputs. Host names use ASCII DNS labels, lowercase canonicalization, and an
optional trailing dot. Duplicate canonical names fail. Empty ALPN lists are
allowed; individual tokens must be unique and 1–255 bytes, with at most 65,535
encoded bytes in total.

Certificate and mutual-TLS profiles are accepted. Mutual-TLS policy requires
explicit roots but does not yet request or enforce client authentication. Other
profiles, including external PSK, fail. All PEM blocks must parse, keys must
match the leaf, and leading/trailing non-whitespace material fails. Encrypted
keys fail without an interactive password callback. Trust roots are parsed into
the context store; no system roots are implicitly loaded and server-leaf trust
verification is not implied. Context construction checks the TLS 1.2 minimum
and disables early application data. RAII frees partial candidates, and provider
error queues are cleared on entry and exit. Failure diagnostics are library-owned
and contain no credentials or raw provider errors.

A short writer mutex serializes generation assignment and atomic publication.
Only successful complete candidates consume identifiers. Failed replacement
preserves the exact active snapshot. Readers atomically load once with acquire
semantics and take no application-level mutex. Publication uses acquire/release
exchange, and retired generations are released outside the writer mutex.
The implementation uses the standard shared-pointer atomic free functions:
Apple's available libc++ lacks the C++20 `atomic<shared_ptr>` specialization.
These atomics are not claimed to be lock-free.

`select()` pins the entire snapshot and its immutable host context. The adapter
passes that owning selection into `tls_session`, which stores it before
`SSL_new`. Established sessions and retiring child completions keep the pin
until teardown. Selection survives registry replacement and destruction.
Old snapshots expire after their final selected/session owner is released.

## RED and wire evidence

- Linkable registry scaffolding rejected all input: initial publication tests
  failed **2 assertions across 2 tests** before validation/publication existed.
- An adapter overload that kept only the context, discarding the snapshot,
  passed certificate observations but failed **2 snapshot lifetime checks**.
  Session ownership made these checks pass.
- The new credential suite exercises invalid material, malformed intermediates
  and roots, mismatched keys, bounded inputs, canonical/default host selection,
  policy rejection, ALPN limits, owned metadata, and failed multi-host candidates.
- Two writers and two readers use start and publication-round barriers, 20
  bounded rounds, and 200 acquisitions per reader per round. Readers verify
  complete metadata/context generations, monotonic observations, and exact
  generation counts at quiescent round boundaries. Invalid writers consume no
  identifier; 40 successful replacements finish at generation 41.
- Rotation uses an independent OpenSSL memory-BIO peer over fragmented transport,
  with explicit root verification and a fresh cache-disabled client context for
  each full handshake. It verifies received leaf serial **101** before rotation
  and for a delayed A selection, then serial **202** for new B acquisitions and
  after invalid C. Root verification also proves transmission of the intermediate
  chain. An established A session still transfers application bytes after rotation.
- Lifetime checks cover selected and established sessions, registry destruction,
  and adapter destruction while raw handshake children retire on the executor.
- Dedicated test-only RSA credentials were generated locally with OpenSSL and
  signed through root/intermediate fixtures, with ten-year validity. They do not
  depend on the legacy certificate fixtures' validity dates.

## Local acceptance

Environment: macOS ARM64, Apple Clang, C++20, Autotools VPATH builds. The genuine
provider was rechecked: `/private/tmp/task129-provider/install/bin/openssl version`
reported **OpenSSL 3.5.9 29 Sep 2026**. Local socket tests run with permission to
bind ephemeral loopback ports. Sandbox-denied listener attempts are not positive
acceptance evidence.

Bootstrap: `./bootstrap`. TLS-on configuration:

```sh
mkdir -p build-on
cd build-on
../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
```

TLS-off uses the same C++20/prerequisite flags, `--disable-v3-tls`, and no
`V3_TLS_*` flags. From the worktree root:

```sh
make -C build-on -j2
make -C build-on/test -j2 \
  tls_credentials tls_credentials_rotation tls_io tls_io_race tls_io_loopback io_tls_control
make -C build-on/test -j1 check-TESTS \
  TESTS='tls_credentials tls_credentials_rotation tls_io tls_io_race tls_io_loopback io_tls_control'
make -C build-on check-v3-native-linkage check-local
make -C build-off -j2
make -C build-off/test -j2 \
  consumer_v3_features io_tls_control io_backend_contract server_options_validate
make -C build-off/test -j1 check-TESTS \
  TESTS='consumer_v3_features io_tls_control io_backend_contract server_options_validate'
make -C build-off check-v3-native-linkage check-local
git diff --check
```

Local results:

- TLS-on and TLS-off C++20 builds passed.
- TLS-on focused acceptance passed **6/6 executables**, **29 tests / 619 checks**,
  with no skips or failures. The new suites account for **6 tests / 177 checks**.
- TLS-off focused acceptance passed **4/4 executables**: the feature consumer
  plus **62 tests / 920 checks** in neutral control/backend/options suites.
  There were no skips or failures.
- Native linkage audits and repository `check-local` passed in both modes.
- ASan/UBSan passed **24 tests / 562 checks** across credentials, rotation,
  adapter, and lifecycle suites. TSan passed **6 tests / 177 checks** across
  credentials and rotation. Neither emitted sanitizer diagnostics.
- Changed-file cpplint, CCN <= 10, repository file-size, and `git diff --check`
  passed. The repository-wide complexity command still reports three unchanged
  v3 violations: `dispatch_request` (11), `valid_peer_pattern` (15), and
  `pollsys::accept_one` (11). A fresh `git show v3:<path>` baseline reproduced
  the same three findings; they are outside TASK-131.
- Existing macOS libtool linker deprecation/duplicate-library warnings remain.


The task started at `492c04c751136d5b20990d497b1be66fde49b866` on the registered
`task/TASK-131` worktree from `v3`. No pristine full-suite baseline is claimed.
An early overlapping build/test invocation briefly lost the shared-library link
while relinking; subsequent fully built acceptance supersedes that attempt.

## Sanitizer reproduction

The bounded task-local probe `/private/tmp/task131-sanitizers.py` compiles these
actual native sources independently for each sanitizer: `io_operation.cpp`,
`io_connection_owner.cpp`, `fake_io_backend.cpp`, `worker_pool.cpp`,
`tls_credentials.cpp`, `tls_session.cpp`, and `tls_io_backend.cpp`.
It uses no previously built unsanitized native archive. Equivalent commands,
run from this worktree, are:

```sh
sh <<'SH'
provider=/private/tmp/task129-provider/install
sources='io_operation io_connection_owner fake_io_backend worker_pool tls_credentials tls_session tls_io_backend'
for sanitizer in address,undefined thread; do
  probe="/private/tmp/task131-reproduce-$sanitizer"
  mkdir -p "$probe"
  for source in $sources; do
    c++ -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
      -fsanitize="$sanitizer" -DHTTPSERVER_COMPILATION \
      -Isrc -Itest -I"$provider/include" \
      -c "src/detail/$source.cpp" -o "$probe/$source.o"
  done
  tests='tls_credentials tls_credentials_rotation'
  if test "$sanitizer" = address,undefined; then
    tests="$tests tls_io tls_io_race"
  fi
  for target in $tests; do
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

The reproduction explicitly uses a POSIX shell. Selected OpenSSL
static libraries are not sanitizer-instrumented. Sanitizer receipts cover the
new concurrent registry and wire/lifetime tests; they do not constitute cloud,
production, or nonlocal platform proof.

Native TLS listener rejection and `query_features().tcp_tls == false` remain.
SNI/ALPN callbacks, client-auth enforcement, PSK, ACME, and QUIC belong to later
tasks. Linux/epoll, BSD, Windows/WSAPoll, and other unavailable platform checks
are unexecuted and assigned to CI/the v3 PR under AGENTS.md. Validation, runner
commits, integration into `v3`, and worktree cleanup remain caller-owned.
