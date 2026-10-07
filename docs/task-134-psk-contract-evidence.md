# TASK-134 external-PSK contract evidence

## Scope and design

[The provider-neutral contract](external-psk-lookup-contract.md) chooses separate
bounded handshake and application-lookup lanes, exclusive SSL/BIO handoff,
nonblocking admission, absolute deadlines and owner-side terminal arbitration.
It defines cooperative cancellation, late-work retirement, pinned immutable
callback/runtime ownership, concurrent invocation, identity representation,
key bounds, zeroization and redacted typed failures. Architecture §4 links the
contract and states that external PSK is still rejected by the current registry.

This task changes documentation and a provider characterization test only.
No PSK profile, public callback API, production execution lane or timeout
behavior is implemented or claimed. TASK-135 must verify the documented runtime
obligations when implementing them.

## Provider observations and plan correction

macOS ARM64, Apple Clang, C++20. The selected provider's current readback was:

```text
/private/tmp/task129-provider/install/bin/openssl version
OpenSSL 3.5.9 29 Sep 2026 (Library: OpenSSL 3.5.9 29 Sep 2026)
```

Configure passed the supported stable 3.5 LTS header/API/link/runtime checks.
The provider source at `/private/tmp/task129-provider/openssl-3.5.9` was
reinspected. Its `ssl.h.in` defines a 256-byte TLS 1.2 wire identity ceiling and
512-byte PSK callback buffer. `tls_process_cke_psk_preamble()` invokes lookup
synchronously, copies the key and cleanses its temporary buffer; TLS 1.3's
`extensions_srvr.c` likewise calls find-session synchronously. Official OpenSSL
callback, ClientHello and ASYNC documentation is linked from the contract.

The saved plan's **48-byte TLS 1.3 key ceiling was disproved**: the selected
provider accepts 49 bytes. `prov_ssl.h::SSL_MAX_MASTER_KEY_LENGTH` is 48, but
`ssl_local.h::TLS13_MAX_RESUMPTION_PSK_LENGTH` is 512 and sizes the session PSK
array. `SSL_SESSION_set1_master_key()` checks that array's size. The final probe
accepts 512 and rejects 513 bytes, retaining the prior 512-byte length after
rejection. The contract uses this observed 512-byte provider ceiling and labels
any tighter application policy separately. This is storage-capacity evidence,
not a TLS 1.3 external-PSK wire exchange or interoperability receipt.

## Characterization and TDD

`tls_psk_contract` is registered only in Automake's `NATIVE_V3_TLS` block and
uses the existing selected-provider flags, linker flags and LT harness. All
fixtures are local to the new test file. Memory BIOs use synthetic PSK material
and existing test-only certificate/key files; no listening socket or shared
executor infrastructure is added.

Three focused tests observe:

- Certificate-based TLS 1.2 and 1.3 ClientHello callbacks return RETRY, yielding
  immediate WANT_CLIENT_HELLO_CB; callback success later resumes both handshakes.
- A TLS 1.2 PSK callback runs on the thread calling `SSL_do_handshake()` and
  receives a 512-byte output buffer. A controlled callback barrier establishes
  that the handshake call remains outstanding until callback release, after
  which the PSK exchange completes.
- The selected provider session PSK storage accepts 512 bytes and rejects 513.

Assertions run on the test thread after barrier release and worker join.
Callback/main waits are bounded at three seconds; handshake pumping is bounded
at 64 rounds. Fixture setup and transfers precede worker creation, and no test
assertion can abandon the worker while the callback is waiting.

Expectations preceded fixture callback behavior. The runnable RED probe with
missing RETRY/barrier/key-return behavior failed 11 checks. Two additional
failures rejected the saved plan's incorrect 48-byte bound. Implementing the
fixture callbacks and correcting that bound produced GREEN: **3 tests / 36
checks**, no failures/skips. The initial standalone compile lacked the existing
LT include path and is not runnable RED evidence. Logs are
`/private/tmp/task134-probe-red.log` and `task134-probe-green.log`.

## Local acceptance

The registered worktree was clean on `task/TASK-134` at `d1349b59`, matching
base `v3`, before task edits. A pristine focused credential baseline passed
**5 tests / 170 checks**; no complete pristine test-suite baseline is claimed.

Bootstrap was `./bootstrap`; separate task-local VPATH builds used:

```sh
mkdir -p build-on build-off
cd build-on
../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
```

TLS-off used the same prerequisite/C++20 flags, `--disable-v3-tls`, and no
`V3_TLS_*` flags. Final commands from the worktree root:

```sh
make -C build-on -j2
make -C build-on/test -j2 \
  tls_psk_contract tls_credentials tls_selection tls_mtls tls_io tls_io_race io_tls_control
make -C build-on/test -j1 check-TESTS \
  check_PROGRAMS='tls_psk_contract tls_credentials tls_selection tls_mtls tls_io tls_io_race io_tls_control' \
  TESTS='tls_psk_contract tls_credentials tls_selection tls_mtls tls_io tls_io_race io_tls_control'
make -C build-on check-v3-native-linkage check-local
make -C build-off -j2
make -C build-off/test -j2 \
  consumer_v3_features io_tls_control io_backend_contract server_options_validate
make -C build-off/test -j1 check-TESTS \
  check_PROGRAMS='consumer_v3_features io_tls_control io_backend_contract server_options_validate' \
  TESTS='consumer_v3_features io_tls_control io_backend_contract server_options_validate'
make -C build-off check-v3-native-linkage check-local
/private/tmp/task129-lint/bin/cpplint test/unit/tls_psk_contract_test.cpp
scripts/check-file-size.sh
git diff --check
```

Results:

- Complete TLS-on and TLS-off C++20 builds passed.
- TLS-on: **7/7 executables**, **40 tests / 1,250 checks**, no failures/skips.
  This includes the new **3 tests / 36 checks** provider probe and existing
  credential, selection, mTLS, adapter and I/O-control regressions.
- TLS-off: **4/4 executables**, the public feature consumer plus
  **63 tests / 937 checks**, no failures/skips.
- Native linkage and `check-local` passed in both modes, including staged
  install/layout and header hygiene checks.
- Changed-file cpplint, repository source file-size and `git diff --check`
  passed. No production C++ source changed.

The first sandboxed TLS-off backend run failed opening loopback listeners
(21 failures); it is not positive evidence. The complete TLS-off focused set
was rerun with authorization to bind ephemeral local listeners and passed.
Existing Autotools and macOS libtool deprecation/duplicate-library warnings
remain. Final build/test/gate logs use `/private/tmp/task134-final-*` and
`/private/tmp/task134-gates-{on,off}.log`; per-executable LT logs are in each
build's `test/` directory.

Linux, BSD, Windows and other unavailable platform checks were not executed;
they belong to CI/the v3 PR under AGENTS.md and do not block local completion.
No sanitizer, full repository test-suite, deployment or production receipt is
claimed. Groundwork validation, runner commits, integration into `v3` and
worktree cleanup remain caller-owned.
