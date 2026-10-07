# TASK-135 external-PSK implementation evidence

Implementation worktree: `task/TASK-135`, based on `v3` at
`4c2ca419bed876c4d810a1b1e263476edad3114f`. Provider: the previously selected
OpenSSL 3.5.9 installation at `/private/tmp/task129-provider/install`; C++20,
macOS local execution. Groundwork validation, commits, publication, merge and
cleanup remain caller/runner-owned. This report establishes implementation-local
acceptance, not deployment or nonlocal portability evidence.

## Supported profiles and ownership

The internal `tls_host_credentials::psk` configuration captures application
lookup and shared runtime in each immutable generation. PSK-only publication
needs no PEM. Certificate/key/trust material, requested/required client
certificates, post-handshake authentication, missing callback/runtime, invalid
bounds and non-HTTP/1 ALPN reject before publication. Failed replacements preserve
the active object and do not consume a generation. Existing server-options
HTTP/1 and client-auth restrictions remain intact and pass regression tests.

| Version | Authentication/cipher policy | Identity and key limits |
| --- | --- | --- |
| TLS 1.2 | `PSK-AES128-GCM-SHA256`, PSK-only AEAD; no forward-secrecy claim | Bounded C-string identity, at most 256 provider bytes; up to 512 key bytes |
| TLS 1.3 | External PSK, `TLS_AES_128_GCM_SHA256`, SHA-256 | Explicit-length identity including embedded NUL; finite policy up to 65535 bytes; up to 512 key bytes |

TLS 1.2 does not promise lossless embedded-NUL identities or strict wire-NUL
rejection. Actual key results are checked against policy and provider capacity;
empty/oversized results reject without truncation. The 512-byte provider bound
is the TASK-134 characterized ceiling, not `SSL_MAX_MASTER_KEY_LENGTH` (48).

Library secure storage is fixed-allocation, move-only and uses the existing
`secure_zero` primitive before release/overwrite/destruction. Tests observe live
bytes after cleansing and before deallocation, never freed memory. Per-attempt
results coalesce duplicate TLS-version/identity lookups and retain bounded secure
keys until handshake completion/failure or session retirement. Temporary secure
copies are wiped after provider copy. This does not prove erasure of OpenSSL's
own copies: provider session/key cleanup remains OpenSSL's responsibility.

TLS 1.3 sessions transfer ownership only after master key, cipher, protocol and
zero early-data limit are installed; partial sessions are freed. Legacy callback
fallback rejects TLS 1.3. PSK caching/tickets/renegotiation are disabled and SNI
explicitly reapplies callbacks/ciphers and selected session namespace. An
independent early-data-capable client offers early bytes, completes the handshake
with `SSL_EARLY_DATA_REJECTED`, then exchanges ordinary authenticated data; the
server read receives only the ordinary request.

## Runtime and lifecycle proof

Separate fixed-worker handshake and lookup lanes reserve finite running/queued
capacity. Admission is nonblocking and never executes application lookup on the
I/O owner. Default budgets are 2+16 outstanding jobs per lane and five seconds;
validated ranges and mixed-host scheduling are documented in
[the installed contract](external-psk-lookup-contract.md#installed-runtime-policy-and-evidence).
A single absolute deadline covers admission queueing, lookup and subsequent
provider steps; the adapter caps missing operation deadlines with runtime policy.

A handshake step owns the session and its ciphertext batches. While it is
outstanding, the serialized owner processes timer/cancel/close events without
accessing SSL/BIO or borrowed application storage. Accepting a result and
publishing success after ciphertext flush both check deadline/terminal state.
A weak owner and close gate prevent late provider work from using a retired
adapter/executor. Held lookup retains owned callback arguments and completion
storage, never SSL, raw transport or application buffers.

Runtime/connection stop reaches the application context. Timeout/cancellation
wakes the trampoline and publishes one logical terminal result while held
application code retains its running slot. `stop(); drain(deadline)` reports
incomplete work until those callbacks return. Runtime destruction signals stop
without an unbounded join. Tests destroy the adapter, raw transport and manual
executor before releasing held lookup, then verify safe retirement under both
sanitizers. Late returned keys are observed cleansed before release.

Independent OpenSSL client SSL objects (separate client-side PSK callbacks and
memory BIOs, real library adapter, scripted fragmented transport) exercise:

- Concurrent distinct identities/keys in both versions and authenticated exchange.
- Unknown/wrong/empty/oversized/rejected/throwing lookup and finite identity policy.
- Binary TLS 1.3 identity; successful 512-byte keys in both versions.
- SNI-selected callback/host/generation, mixed certificate/PSK defaults in both
  directions, and replacement while an old-generation lookup is held.
- Attempt coalescing/budgets, handshake and lookup saturation, queue expiry,
  cancellation before retirement, success/cancel race orders and adapter destruction.
- Exactly one terminal claim, no late peer publication or raw I/O rearm,
  occupied late-lookup capacity, and deadline-bounded incomplete/complete drain.
- HTTP/1 ALPN, rejection of unsupported ALPN/ciphers, old-session replacement
  authentication, and explicit TLS 1.3 early-data rejection.

## TDD observations

Task-local logs under `/private/tmp/task135-*.log` recorded these failures before
implementing the corresponding behavior:

| Probe | RED observation | Result after implementation |
| --- | --- | --- |
| Secure storage | 1 failed check: two live allocations released dirty | Cleansed move/overwrite/destruction passes |
| Runtime execution/admission/deadline | 15 failed checks with rejection-only skeleton | Separate lanes, bounded waits/capacity/drain pass |
| PSK-only publication | Valid publication assertion failed | Valid profile publishes; conflict matrix preserves generation |
| Real PSK mapping | 10 failed authentication/exchange/callback checks | Both independent-client versions authenticate/exchange |
| Exclusive handoff | 4 failed owner responsiveness/held-cancel checks | Owner remains runnable and cancellation completes while lookup is held |
| Per-attempt coalescing | 6 failed status/key/count/budget/wipe checks | Concurrent duplicates invoke lookup once; distinct excess rejects |
| Application runtime stop | Stop token in copied application context was not requested | Combined cancellation and nonjoining destruction pass |

An intermediate held-deadline fixture gave TLS 1.3 too little time to transfer
its fragmented ClientHello before lookup; the corrected 400 ms deadline first
observes lookup entry, then proves expiry while lookup is held. Initial restricted
loopback/transport gates failed listener creation, and are not positive evidence;
the complete focused sets below were rerun with ephemeral local listeners allowed.

## Local commands and results

From the worktree:

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
make -C test -j2 tls_psk tls_psk_runtime tls_psk_contract tls_credentials \
  tls_credentials_rotation tls_selection tls_mtls tls_io tls_io_race \
  tls_io_loopback io_tls_control server_options_validate
make -C test -j1 check-TESTS \
  check_PROGRAMS='tls_psk tls_psk_runtime tls_psk_contract tls_credentials tls_credentials_rotation tls_selection tls_mtls tls_io tls_io_race tls_io_loopback io_tls_control server_options_validate' \
  TESTS='tls_psk tls_psk_runtime tls_psk_contract tls_credentials tls_credentials_rotation tls_selection tls_mtls tls_io tls_io_race tls_io_loopback io_tls_control server_options_validate'
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
git diff --check
```

- TLS-on: **12/12 executables; 86 tests / 1,921 checks**, no failures/skips.
- TLS-off: **4/4 executables**, feature consumer plus **63 tests / 937 checks**,
  no failures/skips. New provider-dependent objects/executables are TLS-on only.
- Native linkage and repository `check-local` gates pass in both configurations.
- ASan/UBSan and TSan each: **41 tests / 1,262 checks** across `tls_psk_runtime`,
  `tls_psk`, `tls_mtls`, `tls_credentials`, `tls_selection`,
  `tls_credentials_rotation`, no sanitizer diagnostics.
- Changed-file cpplint, changed-source CCN <= 10, source file-size gate and
  `git diff --check` pass. Existing macOS libtool linker deprecation and
  duplicate-library warnings remain.

## Sanitizer reproduction and unexecuted checks

`/private/tmp/task135-sanitizers.py` follows the established task-local method.
Equivalent commands from this worktree:

```sh
sh <<'SH'
provider=/private/tmp/task129-provider/install
sources='io_operation io_connection_owner fake_io_backend worker_pool tls_credentials tls_session tls_io_backend tls_psk tls_psk_runtime tls_psk_attempt'
for sanitizer in address,undefined thread; do
  probe="/private/tmp/task135-reproduce-$sanitizer"
  mkdir -p "$probe"
  for source in $sources; do
    c++ -std=c++20 -pthread -O1 -g -fno-omit-frame-pointer \
      -fsanitize="$sanitizer" -DHTTPSERVER_COMPILATION \
      -Isrc -Itest -I"$provider/include" \
      -c "src/detail/$source.cpp" -o "$probe/$source.o"
  done
  for target in tls_psk_runtime tls_psk tls_mtls tls_credentials tls_selection tls_credentials_rotation; do
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

All affected native objects are instrumented independently; no unsanitized
native archive is linked. OpenSSL/system libraries are not instrumented. No
BSD, Windows, Linux or other nonlocal platform receipt, full repository test
suite, MSan, deployment or production receipt is claimed. Nonlocal checks are
assigned to CI/the v3 PR by AGENTS.md and do not block this local implementation
phase. Formal Groundwork validation/finalization is still caller-owned.
