# TASK-139 implementation evidence

Worktree: `/Users/etr/progs/libhttpserver/.worktrees/TASK-139`.
Branch: `task/TASK-139`; base: `v3` at
`7f8ae65c9ad675ed8e78474cbe66332364502d93`. Task and index remain In Progress;
the runner owns validation, commits, merge and cleanup. No staging or commit
was performed during implementation.

## Implemented boundary

The guarded private frame parser consumes the client magic, nine-byte headers
and one frame/event at a time. The caller retains any suffix and releases an
outstanding event before continuing. Non-control payload storage is admitted
against `body_buffer_bytes` before allocation and retained through event
ownership. SETTINGS uses six-byte scratch and a bounded transaction containing
final standard values plus the minimum/final HPACK table-size changes. PING
uses eight-byte scratch. Unknown payloads are skipped without allocation.
Oversized headers fail at nine bytes before payload admission. Frame-size
violations may be escalated to a connection error; malformed PRIORITY provides
a stream-scoped, completely discarded frame with subsequent framing intact.
Continuation ordering and EOF during a field block are enforced.

Each connection owns independent peer/local/queued-local state and two HPACK
directions. Peer SETTINGS changes the encoder; FIFO ACK of local SETTINGS
changes the decoder. Invalid earlier duplicates remain errors, and truncated
transactions cannot change peer state or generate an ACK. Optional advisory
limits start unlimited; omitted advisory values in a local update retain the
last advertised value. Peer MAX_FRAME_SIZE never raises the inbound limit;
all control output is smaller than the minimum legal peer frame size. Legal
large table settings change logical limits without proportional allocation.

Server output starts with SETTINGS. Its first exposure to the output owner
commits the local snapshot and starts the injected monotonic ACK deadline;
construction and unexposed queued updates start no clock. Exact deadline expiry
returns SETTINGS_TIMEOUT. Callers must use the same monotonic clock for output,
input and timeout checks; default epoch arguments support deterministic private
fixtures. ACK with no committed pending update is a connection PROTOCOL_ERROR.

Private defaults are 64 control slots, 4 KiB reserved response-output capacity,
16 queued-or-pending local SETTINGS and 64 completed frames per pump turn.
Smaller test limits are validated before input. `begin_turn()` renews work
allowance; yielding consumes no suffix and preserves protocol state. The
4 KiB charge includes an independent 17-byte terminal GOAWAY path. Peer control
queue overload maps to `limit_exceeded`/ENHANCE_YOUR_CALM; local update admission
returns `limit_exceeded` without corrupting the live connection. SETTINGS/PING
progress despite exhausted DATA buffering. An exposed control frame finishes
before terminal GOAWAY, preserving its borrowed storage and wire boundary.
Terminal failure is sticky and releases input reservations; output reservation
is released when terminal output is consumed or the connection is destroyed.
Hierarchical refusal rolls back without residue. No uncharged error output is
exposed when initial reserve/configuration admission fails.

The private TLS enum copies actual provider-negotiated ALPN only after a
successful authenticated handshake; failed/uncompleted handshakes stay unknown.
The adapter publishes it atomically before completing its successful handshake.
Real OpenSSL independent peers cover TLS 1.2/1.3 h2, HTTP/1.1, absent ALPN and
failed negotiation. A completed h2 adapter decrypts five-byte input fragments
into the machine and encrypts its SETTINGS/ACK/PING output back to the peer.
Existing credentials and fragmented memory-BIO transport fixtures are reused.
The test peer disables certificate verification, as those existing fixtures do;
this is a private protocol composition proof, not public listener or PKIX proof.

There are 28 committed binary RFC 9113 transcripts. The shared corpus loader is
unchanged; the HTTP/2 runner decodes its own typed verdicts and compares exact
consumed offsets, output octets, settings effects, stream/connection scope,
wire codes, EOF and sticky terminal behavior. Every fixture is replayed
coalesced, bytewise and at every split. Fixtures are registered in EXTRA_DIST.

Public listener admission, feature reporting and dispatch remain unchanged.
This task does not serve public HTTP/2 requests, collect HPACK blocks, implement
stream lifecycle, multiplexing, window accounting, fair scheduling or drain.
These responsibilities belong to subsequent tasks.

## TDD observations

Behavioral RED receipts in `/private/tmp`:

- `task139-frame-red.log`: the initial parser stub failed preface consumption,
  single-event boundaries, malformed/truncated magic and frame skipping.
- `task139-settings-red.log`: the initial connection stub failed server output,
  peer SETTINGS effects, FIFO ACK and deadlines.
- `task139-connection-red.log`: exact-budget segmented DATA header admission,
  continuation EOF, initial reserve refusal and terminal accounting failed.
- `task139-stream-red.log`: malformed stream-frame floods bypassed the turn cap;
  a stream error in priority-bearing HEADERS lost continuation ordering.
- `task139-output-red.log`: failure after partial control output lost the
  in-progress frame before GOAWAY.
- `task139-local-settings-red.log`: omitted advisory fields incorrectly reset
  the committed local snapshot.
- `task139-tls-red.log`: successful real handshakes still returned unknown
  ALPN (six session checks and the adapter h2 gate; seven failures).

All corresponding final tests pass. Tests also observe zero declared-length
allocation for unknown payloads and oversized headers, exact queue/byte/pending
limits, stream error resynchronization, binary PING echo, ACK non-response,
reserved bits, undefined flags, DATA pressure and destruction/accounting.
Initial test bring-up compile errors and a TLS executable run from the wrong
working directory are not counted as behavioral RED.

## Local reproduction

macOS arm64; Apple clang 21.0.0 (`clang-2100.1.1.101`); C++20.
Provider rechecked with `/private/tmp/task129-provider/install/bin/openssl version`:
OpenSSL 3.5.9 (29 Sep 2026). Builds are task-local and out of source.

```sh
./bootstrap
mkdir -p build-off build-on
cd build-off
../configure --disable-v3-tls CPPFLAGS=-I/opt/homebrew/include \
  LDFLAGS=-L/opt/homebrew/lib CXXFLAGS='-std=c++20 -O0 -g'
make -C src -j2
make -C test -j2 http2_frame http2_settings http2_connection http2_conformance
make -C test -j1 check-TESTS \
  check_PROGRAMS='http2_frame http2_settings http2_connection http2_conformance' \
  TESTS='http2_frame http2_settings http2_connection http2_conformance'
make check-v3-native-linkage check-local
cd ../build-on
../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g'
make -C src -j2
make -C test -j2 http2_frame http2_settings http2_connection http2_conformance \
  http2_tls_boundary tls_selection tls_credentials tls_io \
  hpack_connection hpack_field_section hpack_primitives
make -C test -j1 check-TESTS \
  check_PROGRAMS='http2_frame http2_settings http2_connection http2_conformance http2_tls_boundary tls_selection tls_credentials tls_io hpack_connection hpack_field_section hpack_primitives' \
  TESTS='http2_frame http2_settings http2_connection http2_conformance http2_tls_boundary tls_selection tls_credentials tls_io hpack_connection hpack_field_section hpack_primitives'
make check-v3-native-linkage check-local
cd ..
python3 -m cpplint src/detail/http2_*.cpp src/httpserver/detail/http2_*.hpp \
  src/detail/tls_session.cpp src/detail/tls_io_backend.cpp \
  src/httpserver/detail/tls_session.hpp src/httpserver/detail/tls_io_backend.hpp \
  test/unit/http2_*.hpp test/unit/http2_*_test.cpp
python3 -m lizard -C 10 --warnings_only src/detail/http2_*.cpp \
  src/httpserver/detail/http2_*.hpp src/detail/tls_session.cpp \
  src/detail/tls_io_backend.cpp src/httpserver/detail/tls_session.hpp \
  src/httpserver/detail/tls_io_backend.hpp
scripts/check-file-size.sh
git diff --check
```

Focused sanitizer reproduction from the worktree root:

```sh
mkdir -p /private/tmp/task139-sanitizers
for source in frame connection; do
  c++ -std=c++20 -DHTTPSERVER_COMPILATION -Isrc -Itest -O1 -g \
    -fno-omit-frame-pointer -fsanitize=address,undefined \
    -c "src/detail/http2_$source.cpp" \
    -o "/private/tmp/task139-sanitizers/$source.o"
done
for target in frame settings connection conformance; do
  c++ -std=c++20 -DHTTPSERVER_COMPILATION -Isrc -Itest -O1 -g \
    -fno-omit-frame-pointer -fsanitize=address,undefined \
    -DHTTP2_CORPUS_DIR='"test/conformance/http2"' \
    "test/unit/http2_$target"_test.cpp \
    /private/tmp/task139-sanitizers/frame.o \
    /private/tmp/task139-sanitizers/connection.o \
    -o "/private/tmp/task139-sanitizers/$target"
  "/private/tmp/task139-sanitizers/$target" || exit
done
```

The supplied registered runner path/branch was used verbatim. The installed
identity helper is incompatible with this checkout: from the worktree it
suggested a nested worktree, and from the repository root it rejected a missing
monorepo project name. `git worktree list --porcelain`, HEAD and branch readback
confirmed the supplied existing identity instead; no worktree was created.

## Results and limits

- Initial TLS-off adjacent HPACK baseline: 3/3 executables, 21 tests / 1,577
  checks, no failures. `/private/tmp/task139-baseline-tests.log`.
- TLS-off focused: 4/4 executables, 20 tests / 17,164 checks, no failures/skips.
  `/private/tmp/task139-off-tests.log`, plus `build-off/test/http2*.log`.
- TLS-on focused and adjacent: 11/11 executables, 64 tests / 19,452 checks,
  no failures/skips. `/private/tmp/task139-on-tests.log`, plus
  `build-on/test/{http2*,hpack*,tls_selection,tls_credentials,tls_io}.log`.
- Native linkage and repository check-local pass in both builds, including
  header/example/documentation checks and staged consumer hygiene.
  `/private/tmp/task139-{off,on}-gates.log`.
- Changed-file cpplint, changed-source CCN <= 10, repository source file-size
  and diff whitespace checks pass. Receipts:
  `/private/tmp/task139-{lint,complexity,size}.log`.
- ASan/UBSan: all four focused HTTP/2 executables pass; native HTTP/2 objects and
  inline budgets/HPACK code are instrumented. No provider/TLS, system library,
  full-suite, TSan, MSan, deployed or production sanitizer claim is made.
  `/private/tmp/task139-sanitizers.log`. The final conformance assertion cleanup
  was rebuilt in both normal configurations and ASan/UBSan; receipts are
  `/private/tmp/task139-conformance-{off,on,sanitizers}-final.log`.

An initial overlapping make invocation caused a libtool temporary archive
member collision; sequential rebuilds repaired that build artifact and all
final gates passed. Existing macOS libtool deprecation/duplicate-library linker
warnings remain. No unresolved local or baseline-only gate failure remains.
BSD, Windows, Linux-specific backends and other nonlocal platforms are
unexecuted and assigned to CI/the v3 PR by AGENTS.md, without blocking local
completion. Formal Groundwork validation/finalization remains runner-owned.
