# TASK-155 QUIC TLS bridge

The internal `quic_tls_session` adapts an ordinary OpenSSL TLS session to owned
Initial, Handshake and Application CRYPTO streams. It reuses the credential
snapshot, SNI selection, verification and ALPN callbacks used by TCP. QUIC mode
is determined by its private construction path, independently of listener
metadata used by existing TCP selection tests.

## Ownership and admission

All operations require serialization by the connection owner. The referenced
`quic_key_state` must outlive the facade. TLS frees its SSL object before callback
state or borrowed provider buffers are destroyed. No provider type appears in
the facade header.

Input uses three bounded `quic_reassembly` instances. A fixed receive lease is
reserved and allocated at construction, before any contiguous input is consumed.
Additional fragments cannot change its address or contents. Release validates
the saved lease size; a protection-level change cannot consume another stream.
Configuration CIDs and local transport parameters are copied. Peer parameters
are copied into reserved storage and validated against the retained CID facts
before publication. Peer byte views are borrowed from the facade.

Each outbound level has a bounded owned buffer and independent monotonic offsets.
Partial sends acknowledge precisely the accepted prefix; a full buffer accepts
zero bytes and lets OpenSSL request a write retry. `copy_output` preserves bytes
for future retransmission. The connection owner must explicitly retire a prefix
when its delivery policy permits it. This bridge provides no ACK or loss policy.
Fixed buffer capacities and dynamically retained reassembly storage are charged
to `quic_reassembly_bytes`, including the receive lease and parameter backing.
Failed receive admission leaves the stream unchanged; construction failure rolls
back reservations. Provider callback failures remain terminal.

## TLS policy

Only TLS 1.3 certificate profiles permitting `h3` may complete QUIC handshakes.
SNI switches reapply verification, cipher policy, TLS version limits and the zero
early-data byte cap on the SSL object. TCP continues selecting its existing HTTP
protocols; QUIC cannot select ACME credentials or an external-PSK HTTP/1 profile.
The acquired immutable credential generation remains alive through SSL teardown.

`SSL_set_quic_tls_early_data_enabled(ssl, 0)` is called during callback
installation. OpenSSL 3.5.9 requires `SSL_in_before()` for that setter, so calling
it during an SNI callback is invalid. SNI selection retains the disabled QUIC
state and reapplies `SSL_set_max_early_data(ssl, 0)` without changing published
contexts. This is the provider-driven adjustment to the saved plan. Initial
client-certificate authentication remains effective; published contexts disable
post-handshake client authentication.

Secrets map explicitly from provider levels/directions and cipher IDs. Read and
write levels advance independently only after successful owned key installation.
EARLY, repeated or reversed transitions, invalid directions and unsupported suites
fail closed. Initial keys remain the connection owner's responsibility. There is
no additional raw-secret copy in the bridge.

Post-handshake processing calls the provider read path with a zero-length request
so TLS tickets and other post-handshake CRYPTO are processed without an application
TLS data API. Failures retain typed causes and optional TLS alert codes; the
connection owner remains responsible for QUIC close packets.

## Local evidence, 2026-10-08

Provider verification reported OpenSSL **3.5.9**, from
`/private/tmp/task129-provider/install`. All builds used C++20 and bounded compiler
parallelism in task-local VPATH directories.

| Check | Result |
|---|---|
| Baseline reassembly, key-state, crypto and parameter executables | 4/4 pass |
| Final TLS-on focused executables | 13/13 pass, no skips |
| New callback storage tests | 7 cases, 194 checks pass |
| New independent handshake tests | 5 cases, 70 checks pass |
| TLS-off provider-free focused executables | 4/4 pass; new TLS executables absent from `check_PROGRAMS` |
| ASan/UBSan new callback and handshake executables | 2/2 pass, no sanitizer diagnostics |
| Native linkage audits, TLS-on and TLS-off | Both pass |
| Changed C++ files: cpplint and CCN <= 10 | Pass |
| Warning-suppression scan and whitespace diff | Pass |

The independently buffered raw OpenSSL client verifies trust and hostname,
negotiates exactly `h3`, exchanges transport parameters, and observes secrets
matching the server's opposite-direction installed keys. The transport reverses
17-byte client CRYPTO fragments, starts a handshake with a delayed one-byte
fragment during rotation, and uses a 127-byte server output capacity and 53-byte
receive leases to exercise retries and fragmented input. Ownership tests overwrite
source buffers, add fragments during outstanding leases, transition levels before
release, leave leases outstanding at teardown, and inject allocation/budget
failures. Invalid transport parameters, directions, suites and transitions are
rejected with retained terminal errors.

Tickets obtained through post-handshake processing are resumable and advertise
zero early-data allowance. Reconnection proves actual session reuse. A subsequent
client uses an eligible copy of a captured ticket: its outgoing ClientHello
message callback confirms the early-data extension is present, the handshake
completes, and the client reports `SSL_EARLY_DATA_REJECTED`. Successful completion
also proves the server's fail-closed EARLY-secret callback was never invoked.
The fixture uses separate callbacks and buffers from the production bridge.
SNI certificate/ALPN selection, pinned rotation, required client authentication,
ACME isolation and external-PSK exclusion are exercised. Existing TCP selection,
rotation, mTLS, ACME, PSK and HTTP/2 TLS boundary tests pass.

The reused OpenSSL archives are **not sanitizer-instrumented**. The library and
new tests are instrumented. Global file-size and complexity gates retain exactly
the same baseline failures as an archive of `14388d23`: `io_poll_backend.cpp` has
509 SLOC against the 500 ceiling, and 17 existing functions exceed CCN 10. None of
the changed files introduces a size or complexity violation. Thresholds and
unrelated source were preserved.

## Reproduction

```sh
./bootstrap
mkdir -p build/task155-on
cd build/task155-on
../../configure --enable-v3-tls --disable-examples \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -C src -j3 libhttpserver_v3core.la
make -C test -j3 quic_tls_session quic_tls_handshake quic_reassembly \
  quic_key_state quic_crypto quic_transport_parameters tls_selection \
  tls_credentials_rotation tls_mtls tls_acme_selection http2_tls_boundary \
  tls_psk v3_native_linkage
make -C test check \
  check_PROGRAMS='quic_tls_session quic_tls_handshake quic_reassembly quic_key_state quic_crypto quic_transport_parameters tls_selection tls_credentials_rotation tls_mtls tls_acme_selection http2_tls_boundary tls_psk v3_native_linkage' \
  TESTS='quic_tls_session quic_tls_handshake quic_reassembly quic_key_state quic_crypto quic_transport_parameters tls_selection tls_credentials_rotation tls_mtls tls_acme_selection http2_tls_boundary tls_psk v3_native_linkage'
```

TLS-off uses a separate build directory with `--disable-v3-tls` and no provider
flags, building/running `quic_reassembly quic_stream_state
quic_transport_parameters v3_native_linkage`. The sanitizer build uses another
directory with `-fsanitize=address,undefined -fno-omit-frame-pointer` in
`CXXFLAGS`, and `-fsanitize=address,undefined` in `LDFLAGS`; it runs the two new
executables. Run `scripts/audit-v3-native-linkage.sh` with `BUILD_DIR` pointing to
the applicable build and `V3_TLS_MODE=yes` or `no`.

This is **TLS-level independent-client handshake evidence**. UDP QUIC
interoperability, packet recovery and HTTP/3 requests remain outside this task.
BSD, Windows and other nonlocal platform checks were not executed locally; they
belong to CI and the v3 PR under `AGENTS.md`. Groundwork validation, runner-owned
commits, merge into `v3` and cleanup remain subsequent phases.
