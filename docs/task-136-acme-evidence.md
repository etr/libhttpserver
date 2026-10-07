# TASK-136 ACME TLS-ALPN-01 implementation evidence

Implementation worktree: `task/TASK-136`, based on `v3` at
`01798ca8b058119358d96bff8ec2413e2fc69d7e`. Local platform: macOS, C++20.
The provider was rechecked using
`/private/tmp/task129-provider/install/bin/openssl version`: OpenSSL 3.5.9.
The runner owns commits, validation, merge and cleanup. Task status remains
In Progress until the caller completes those phases.

## Implemented contract

The private credential registry accepts `tls_acme_challenge` containing a DNS
name, one leaf certificate, an unencrypted matching private key, the expected
32-byte authorization digest and an explicit future deadline. Validation requires
exactly one SAN extension with exactly one DNS entry matching the canonical name,
and exactly one critical `1.3.6.1.5.5.7.1.31` extension whose value is precisely
`04 20` followed by the expected digest. Digest comparison uses `CRYPTO_memcmp`.
Strict bounded PEM parsing rejects multiple leaves, trailing material and key
mismatches. Certificate validity must include publication time; the deadline
must not extend beyond certificate expiry. Provider diagnostics and caller PEM
are not included in failure outcomes or retained in published state.

Publication requires an initialized normal snapshot. Publish replaces one exact
name and removal is idempotent for absent valid names. Successful changes consume
one generation; invalid candidates, malformed removals, no-op removal and
generation overflow preserve active state. Challenges occupy a separate immutable
map, with contexts shared by snapshot copies. All writers use the existing
publication mutex. Normal replacement copies the latest challenge map while
holding that mutex, preventing lost concurrent updates. Expiry is checked at
lookup, including against previously acquired snapshots; no cleanup thread is
required. A selected handshake may finish after its deadline.

The session and adapter accept defaulted provider-neutral `tls_handshake_context`
metadata. Only trusted TCP/local-port 443, exact canonical SNI and a complete ALPN
extension offering solely `acme-tls/1` can select a challenge. Unknown transport,
unknown port, other ports, QUIC, missing/nonmatching SNI, mixed offers and malformed
extensions cannot expose a challenge. A sole offer without an eligible challenge
fails before certificate delivery, including normal hosts with empty ALPN and
snapshotless sessions. Ordinary HTTP selection and generation/host resumption
continue through the existing path. Public listener integration remains a later
task; this task does not infer the listener port from peer-controlled input.

Challenge selection explicitly reapplies certificate ciphers, clears external
PSK callbacks and disables client-certificate verification. It disables early
data, tickets, caching and renegotiation through context and per-SSL restrictions,
a non-resumable-session callback and a fresh per-handshake session-ID namespace.
A TLS 1.3 HelloRetryRequest retains the already-selected context and namespace;
its second ClientHello must keep the exact name and sole challenge offer.
Challenge application reads/writes fail with protocol_error and zero bytes,
while TLS close_notify shutdown remains usable.

## TDD and behavioral observations

| Probe | RED | GREEN |
| --- | --- | --- |
| Publication with rejection-only declarations/stubs | 3 failed checks across two tests | Valid ownership/publication/removal and invalid-candidate matrix pass |
| Real ClientHello selection with metadata accepted but ignored | 4 handshake assertions failed, including ordinary mTLS and external PSK initial contexts | TLS 1.2 and 1.3 certificate/ALPN selection and authentication isolation pass |
| Lifetime/adapter selection | 2 failed old-snapshot/adapter handshake assertions | Acquired handshakes, registry destruction, retirement and ordinary data pass |
| TLS 1.3 retry crossing challenge expiry | 2 failures: ordinary serial 101 delivered instead of challenge serial 303, with wrong digest | Same selected challenge remains pinned; fresh lookup is expired |

Behavioral RED logs: `/private/tmp/task136-credentials-red.log`,
`task136-psk-red.log`, `task136-lifetime-red.log`, `task136-retry-red.log`.
Initial compiler errors while bringing up test declarations/fixtures are not
counted as behavioral RED evidence.

Independent OpenSSL peers use separate client SSL objects and memory BIOs with
fragmented transfers. They inspect the received leaf serial, critical digest
extension, negotiated ALPN, certificate-message count, SNI acknowledgement,
actual tickets and session-reuse disposition. Challenge peers disable PKIX
verification to permit the critical ACME extension; they directly validate the
observed challenge identity instead. Ordinary verified-peer resumption and
rotation regressions remain in the focused suite.

Barrier-controlled writers publish independent names concurrently with ordinary
replacement. A separate simultaneous publication/removal/replacement race proves
that removed names cannot be restored by a normal replacement. Readers pin old and fresh generations while replacement runs,
correlating generation with observed serial, digest and ALPN. Distinct replacement
digests prove coherent old/new contexts. Invalid writers leave the exact active
snapshot and generation unchanged. Both removal/replacement orderings preserve
unrelated challenges. Retirement tests observe weak snapshot/context ownership,
a terminal claim that cannot be taken twice, zero pending raw children and zero
owner completions after executor drain. Established ordinary application data
still exchanges after publication/removal. The retry-expiry test first observes
an actual HelloRetryRequest, then yields until its explicit deadline with a
bounded steady-clock stop; it does not use a scheduling sleep as a race oracle.

## Reproduction

```sh
./bootstrap
mkdir -p build-on build-off
cd build-on
../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -j2
make -C test -j2 tls_acme_credentials tls_acme_selection tls_acme_lifetime \
  tls_credentials tls_credentials_rotation tls_selection tls_mtls tls_psk \
  tls_psk_contract tls_psk_runtime tls_io tls_io_race tls_io_loopback \
  io_tls_control server_options_validate
make -C test -j1 check-TESTS \
  check_PROGRAMS='tls_acme_credentials tls_acme_selection tls_acme_lifetime tls_credentials tls_credentials_rotation tls_selection tls_mtls tls_psk tls_psk_contract tls_psk_runtime tls_io tls_io_race tls_io_loopback io_tls_control server_options_validate' \
  TESTS='tls_acme_credentials tls_acme_selection tls_acme_lifetime tls_credentials tls_credentials_rotation tls_selection tls_mtls tls_psk tls_psk_contract tls_psk_runtime tls_io tls_io_race tls_io_loopback io_tls_control server_options_validate'
make check-v3-native-linkage check-local
cd ../build-off
../configure --disable-v3-tls CPPFLAGS=-I/opt/homebrew/include \
  LDFLAGS=-L/opt/homebrew/lib CXXFLAGS='-std=c++20 -O0 -g'
make -j2
make -C test -j2 consumer_v3_features io_tls_control io_backend_contract server_options_validate
make -C test -j1 check-TESTS \
  check_PROGRAMS='consumer_v3_features io_tls_control io_backend_contract server_options_validate' \
  TESTS='consumer_v3_features io_tls_control io_backend_contract server_options_validate'
make check-v3-native-linkage check-local
cd ..
scripts/check-file-size.sh
python3 -m lizard -C 10 --warnings_only src/detail/tls_credentials.cpp \
  src/detail/tls_session.cpp src/detail/tls_io_backend.cpp \
  src/httpserver/detail/tls_credentials.hpp src/httpserver/detail/tls_io_backend.hpp \
  src/httpserver/detail/tls_session.hpp
cpplint src/detail/tls_credentials.cpp src/detail/tls_session.cpp src/detail/tls_io_backend.cpp \
  src/httpserver/detail/tls_credentials.hpp src/httpserver/detail/tls_io_backend.hpp \
  src/httpserver/detail/tls_session.hpp test/unit/tls_acme_*.hpp test/unit/tls_acme_*_test.cpp
git diff --check
```

The baseline `tls_credentials` and `tls_selection` executables passed before
implementation. The three new executables are TLS-on-only Automake targets;
runtime-generated certificates add no static PEM assets. Initial restricted
loopback attempts failed listener creation in `tls_io_loopback` and
`io_backend_contract`; those attempts are environment failures. The same suites
were rerun with ephemeral local listener permission and passed.

## Final local results

- TLS-on: **15/15 executables; 101 tests / 2,474 checks**, no failures/skips.
  `/private/tmp/task136-final-focused.log` records the full set; the added
  simultaneous-removal test was rebuilt and rerun separately in
  `/private/tmp/task136-removal-concurrent.log` (6 lifetime tests / 93 checks).
- TLS-off: **4/4 executables**, feature consumer plus **63 tests / 937 checks**,
  no failures/skips, in `/private/tmp/task136-off-test-unrestricted.log`.
- Native linkage and repository `check-local` pass in both configurations,
  including headers, examples, documentation and staged consumer hygiene.
- ASan/UBSan and TSan: the nine focused sanitizer executables each pass;
  final aggregate **56 tests / 1,815 checks** per sanitizer, no diagnostics.
  `/private/tmp/task136-final-sanitizers.log` records the full rebuild;
  `task136-final-acme-sanitizers.log` and
  `task136-removal-concurrent-sanitizers.log` record final strengthened
  digest/concurrent-removal test rebuilds against those instrumented objects.
- Changed-file cpplint, changed-source CCN <= 10, source file-size and
  `git diff --check` gates pass. Existing macOS libtool linker deprecation and
  duplicate-library warnings remain.

## Sanitizer reproduction

`/private/tmp/task136-sanitizers.py` uses the task-local native dependency closure
from TASK-135, with all affected native objects instrumented. Equivalent rebuild:

```sh
sh <<'SH'
provider=/private/tmp/task129-provider/install
sources='io_operation io_connection_owner fake_io_backend worker_pool tls_credentials tls_session tls_io_backend tls_psk tls_psk_runtime tls_psk_attempt'
for sanitizer in address,undefined thread; do
  probe="/private/tmp/task136-reproduce-$sanitizer"
  mkdir -p "$probe"
  for source in $sources; do
    c++ -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
      -fsanitize="$sanitizer" -DHTTPSERVER_COMPILATION \
      -Isrc -Itest -I"$provider/include" \
      -c "src/detail/$source.cpp" -o "$probe/$source.o"
  done
  for target in tls_acme_credentials tls_acme_selection tls_acme_lifetime tls_psk_runtime tls_psk tls_mtls tls_credentials tls_selection tls_credentials_rotation; do
    c++ -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
      -fsanitize="$sanitizer" -DHTTPSERVER_COMPILATION \
      -Isrc -Itest -I"$provider/include" -DTLS_TEST_DIR="\"$PWD/test\"" \
      "test/unit/${target}_test.cpp" "$probe"/*.o \
      -L"$provider/lib" -lssl -lcrypto -o "$probe/$target"
    "$probe/$target" || exit
  done
done
SH
```

No uninstrumented native archive is linked. OpenSSL and system libraries are not
instrumented. BSD, Windows, Linux and other nonlocal checks are unexecuted here
and assigned to CI/the v3 PR by AGENTS.md; they do not block local completion.
No full-repository test-suite, deployment, production, MSan or nonlocal receipt
is claimed. Groundwork validation and finalization remain caller-owned.
