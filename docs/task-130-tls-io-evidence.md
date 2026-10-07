# TASK-130 private nonblocking TCP TLS adapter

The private `tls_io_backend` layers a bounded BIO-pair session over existing
owned raw read, write, and timer operations. The adapter admits one plaintext
read and one plaintext write concurrently; handshake and shutdown are exclusive.
Ciphertext buffers remain owned until raw child completion. Application buffers
remain borrowed until their application operation completes.

Every SSL/BIO call runs in the session's coalesced, nonreentrant pump, including
when its executor has multiple workers. WANT outcomes park on raw I/O, buffered
output is flushed, and partial ciphertext writes retain their offset. SSL write
retries retain the same plaintext arguments. Errors are classified immediately
on the calling thread after clearing the provider error queue. Results expose
only library-owned outcome codes.

Control handles carry absolute deadlines. Plaintext read/write handles optionally
carry an absolute deadline consumed by the TLS adapter; raw backends continue to
use explicit timer operations. Cancellation or timeout aborts the session: the
initiating operation receives `cancelled` or `timeout`, other admitted operations
receive `connection_closed`, and losing child operations are cancelled. Raw
controls accidentally submitted to a socket backend complete `not_supported`.
TLS `request_cancel()` accepts a serialized cancellation event; the operation's
completion result determines the winner of a race with success.

A received close_notify yields a zero-byte `connection_closed` read. Graceful
shutdown waits for both close_notify messages and transmission of generated
ciphertext. A raw read termination without close_notify is a truncated TLS stream
and yields `protocol_error`; raw write failure yields `connection_closed`.
Explicit adapter close yields `connection_closed`. Call `close()` before raw
connection release during intentional teardown, drain the executor, and retain
the raw backend and executor through all retiring child completions. Fatal TLS
errors abort without attempting graceful shutdown.

The owning context seam is private. The client context is unverified and is used
by the transport fixtures; this task creates no public client API, credential
publication API, listener credential configuration, SNI, or ALPN selection.
Native TLS listeners remain rejected and `query_features().tcp_tls` remains
false. HTTP/2 and HTTP/3 remain unavailable.

## Local environment and red evidence

- macOS ARM64, Apple Clang, C++20, Autotools VPATH builds `build-on` and `build-off`.
- Genuine, unmodified OpenSSL 3.5.9 headers and runtime at
  `/private/tmp/task129-provider/install`; `openssl version -a` reports stable
  `OpenSSL 3.5.9 29 Sep 2026`. No rejection fixture was used for positive proof.
- Behavioral RED against linkable adapter stubs: 8 failures across handshake
  parking, child retirement/cancellation, and handshake timeout.
- Subsequent RED: 12 failures for missing plaintext deadlines and raw control
  rejection; 4 failures for cancellation operation/foreign target handling;
  2 failures for oversized PEM input bounds; 2 failures for raw rejection
  sequence metadata; 2 failures for retained context/extra destructor work after
  close. Corresponding assertions passed after implementation.
- The initial checkout was clean at `ebe40393bf7839a262f2cf0e6c5b5cac6674ad1c`;
  no pristine full-suite test run is claimed.

## Final local results

- TLS-on C++20 build passed; focused checks passed **10/10** executable targets.
- TLS-off C++20 build passed; focused checks passed **6/6** executable targets.
- The adapter, lifecycle, loopback, and neutral control suites passed **23 tests,
  442 checks**, without skips. Lifetime assertions include release of the owning
  context and no newly posted destructor work after close and executor drain.
- Native linkage audits and repository `check-local` passed in both modes.
- ASan/UBSan passed the three TLS suites (**21 tests, 433 checks**); TSan passed
  the lifecycle suite (**9 tests, 244 checks**). No sanitizer diagnostics occurred.
- Changed-file cpplint and CCN checks, repository file-size check, and
  `git diff --check` passed. The unchanged repository complexity failures below
  remain recorded rather than reported as passes.

## Reproduction

Bootstrap with `./bootstrap`. Configure TLS-on with:

```sh
mkdir -p build-on
cd build-on
../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
```

Configure `build-off` with the same local prerequisite and C++20 flags, replacing
the provider selection with `--disable-v3-tls` and omitting `V3_TLS_*` flags.
Local socket tests require permission to bind ephemeral loopback ports. Sandbox
runs denied listener creation; those failures are not positive TLS proof.

```sh
make -C build-on -j2
make -C build-on/test -j1 check \
  TESTS='tls_io tls_io_race tls_io_loopback io_tls_control io_operation io_connection_owner fake_io_backend io_backend_contract external_readiness_adapter io_kqueue_backend'
make -C build-on check-v3-native-linkage
make -C build-on check-local
make -C build-off -j2
make -C build-off/test -j1 check \
  TESTS='consumer_v3_features io_tls_control io_operation io_backend_contract native_http1_e2e v3_header_hygiene'
make -C build-off check-v3-native-linkage
make -C build-off check-local
git diff --check
```

The TLS suites cover fragmented handshake, full duplex plaintext, partial
ciphertext sends, 100,000-byte writes under bounded output backpressure, orderly
EOF, malformed records, truncation, graceful and stalled shutdown, deadlines,
stop-token cancellation for every TLS operation kind, success/timeout/cancel/close
orders, destroyed awaiters, dropped handles, and four-worker executor progress
while a TLS connection is stalled. Independent OpenSSL socket-BIO peers exercise
real port-0 TCP round trips with poll, macOS kqueue, and external readiness.

ASan/UBSan builds compile the adapter and its operation/transport dependencies
with `-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer`; contract,
lifecycle, and independent loopback suites run locally. TSan compiles the
lifecycle suite with `-fsanitize=thread` and the same source dependencies.
The selected OpenSSL static libraries are not sanitizer-instrumented.

Changed C++ files pass cpplint and the CCN <= 10 check. The repository-wide
complexity gate also reports three unchanged v3 baseline violations:
`dispatch_request` (11), `valid_peer_pattern` (15), and `pollsys::accept_one` (11).
The same violations were reproduced from `git show v3:<path>` snapshots; they are
outside TASK-130. Existing macOS libtool linker deprecation/duplicate-library
warnings remain in build output.

Linux/epoll, nonlocal BSD, Windows/WSAPoll, and other unavailable platform lanes
are unexecuted and belong to CI and the v3 PR. Local adapter evidence establishes
neither configurable TLS listeners nor public credential integration. Validation,
runner commits, merge into `v3`, and worktree cleanup remain caller-owned.
