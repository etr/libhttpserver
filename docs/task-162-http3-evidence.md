# TASK-162 HTTP/3 smoke evidence

The endpoint is a bounded **test-only** loopback UDP owner composing libhttpserver's
admission/Retry, packet protection, OpenSSL QUIC TLS, recovery, negotiated flow
control, HTTP/3 framing and semantic route engine. Public listener enablement and
TASK-163 interop-runner packaging remain separate. No external server substitutes
for the library endpoint.

## Reproduction

Run from the task worktree, based on v3 commit
`6cb733acf966065250b17e8dc2c3b762ce470749`. Build commands use C++20, verified in
configure and actual compiler output. The local compiler was Apple Clang 21.0.0;
the QUIC TLS provider was OpenSSL 3.5.9 in
`/private/tmp/task129-provider/install` (reused read-only).

```sh
bash bootstrap
mkdir -p build/task162-on
cd build/task162-on
../../configure --enable-v3-tls --disable-examples \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib CXXFLAGS='-O1 -g'
make -C src -j4 libhttpserver_v3core.la
make -C test -j4 http3_network_owner http3_udp_fixture
cd ../..
```

Provision clients explicitly; the gate never downloads tools. The successful
Python environment used Python 3.13.13 and the exact dependency pins in
`test/integ/http3-client-requirements.txt`, including aioquic 1.3.0.
The second adapter used quic-go v0.55.0 with committed go.mod/go.sum and Go
1.25.14. Its official darwin-arm64 archive SHA-256 was
`5b26c0b6f308240fca2614fb02f622cfcc8c0cc3b69c78bba4845489a4590259`.

```sh
/opt/homebrew/bin/python3.13 -m venv build/http3-venv
build/http3-venv/bin/python -m pip install -r test/integ/http3-client-requirements.txt
cd test/integ/http3-client-go
../../../build/tooling/go/bin/go build -o ../../../build/http3-client-go .
cd ../../..
python3 test/integ/http3_gate_runner_test.py
python3 scripts/run-v3-http3-gates.py --build-dir build/task162-on \
  --log-dir build/http3-evidence-final-source \
  --python "$PWD/build/http3-venv/bin/python" --go-client build/http3-client-go
```

Use a new log directory each time; an existing evidence directory is rejected
and preserved. For a task-local Go cache, set GOPATH/GOCACHE to absolute paths
under `build/`. Supply the matching provider/toolchain paths on other machines.

## Observed matrix

Both client identities are checked before starting the fixture. Both use their
own native QUIC/TLS/H3 implementation over real UDP with certificate validation.
Each positive matrix uses one live QUIC connection; wrong-CA uses a separate
connection and native TLS failure inspection.

| Case | aioquic 1.3.0 | quic-go v0.55.0 |
| --- | --- | --- |
| Verified handshake | TLS 1.3, h3, QUIC v1, no early data | TLS 1.3, h3, QUIC v1, no early data |
| GET `/hello` | 200, exact `http3 fixture` | 200, exact `http3 fixture` |
| Incremental POST `/echo` | Exact binary body, including NUL and ff | Exact binary body, including NUL and ff |
| Concurrent `/hold` + `/health` | Distinct streams; 204 completes while hold remains blocked | Same |
| Cancellation + sibling GET | Native stream error 268, server cancellation witness, sibling succeeds | Same |
| Wrong CA | Native TLS transport failure 298, no successful h3 exchange | Same |
| Missing route | 404 | 404 |

The runner compares receipts against expected bytes/status, distinct stream IDs,
server route witnesses and shared connection identity. It checks the held handler
before release/cancellation and checks cancellation before the sibling request.
POST bytes are `6669727374007365636f6e64ff746869726400`, sent in three-byte chunks.

Successful normal matrices were retained under `build/http3-evidence-prefix`,
`build/http3-evidence-repeat` and `build/http3-evidence-final-source`. The final
ASan/UBSan matrix is `build/http3-evidence-sanitize-final`. Each contains:

- `manifest.json`: pinned tools, source HEAD, owned source hashes, working diff
  hash, fixture binary hash and complete client receipts.
- `<client>/receipt.json`, `exits.json`, and process stdout/stderr: exact commands,
  bounded diagnostics and exit statuses.
- `<client>/packets.pcap`: actual received datagram bytes and successfully emitted
  bytes captured at the fixture's UDP boundary. IPv4/UDP envelopes are synthesized
  from the actual loopback endpoints, with raw IPv4 pcap link type 101; this is
  application-boundary capture, not a kernel capture.
- `<client>/tls.keys` (mode 0600) and native qlog/sqlog for positive and failed TLS
  connections; ephemeral CA/leaf/wrong-CA material remains in the owned log tree.
- `<client>/artifacts.json`: parsed capture counts/byte totals checked against
  fixture actual-send/receive accounting, plus validated diagnostic paths.

These ignored task-local logs include ephemeral secrets and are not committed.
They remain available in this worktree for caller review. The checked-in runner
recreates them from fresh credentials. A successful runner checks diagnostics and
teardown as well as request receipts; it cannot succeed on a client receipt alone.

## RED/GREEN and owner invariants

The owner tests first failed without the real composition; reliable control tests
failed before HANDSHAKE_DONE/STOP_SENDING retention existed. Owner tests now verify
authenticated duplicate suppression, one-time admitted Initial credit, failed
atomic-send amplification refund, fresh packet-number reprotection of retained
CRYPTO, successful-send debit and storage release after partial handshake teardown.
The reliable control test checks unsent/lost controls, new packet numbers and
single ACK completion. The endpoint uses admission CID facts and validates peer
transport parameters before constructing flow state.

Real-client RED runs exposed long/short-header protection accounting mistakes,
the Go request-body contract, and a cancellation-sibling stall. The latter's
qlog showed receive-abandonment STOP_SENDING followed by aioquic RESET_STREAM
error 0 while response DATA remained pending. The fixture now finishes serialized
receive turns before draining manual-executor handlers and revisits buffered
prefixes after application admission. It also consumes synchronous UDP completion
and executor work before parking. Both clients then passed repeated matrices and
the instrumented matrix. Failed runs and their packet/key/qlog diagnostics remain
under the earlier `build/http3-evidence-*` directories.

The owner is explicitly bounded: four fixture connections, 64 stream lifetime
records per connection, 16 active requests, 15-second connection lifetime, copied
CRYPTO/output bounds, bounded handler turns, 16-MiB capture and process-output
bounds. Recovery and amplification commit only on successful atomic UDP emission;
unsent plans are abandoned/refunded. Poll readiness and recovery/pacing deadlines
drive progress. Closing the backend drains executor completions, removes CID
registrations, destroys connections and checks listener pending-operation count.

## Local checks and evidence limits

- TLS-on focused suite: **21/21 passed**; TLS-off focused suite: **14/14 passed**.
- ASan/UBSan focused suite: **16/16 passed**; both instrumented network matrices
  passed with `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
  The library/owner/fixture are instrumented; the reused OpenSSL provider and
  independent clients are not. LeakSanitizer proof is not claimed.
- TLS-on and TLS-off `check-local` passed: headers, feature boundaries,
  documentation/staged installation and hygiene checks.
- Six runner tests passed, including fail-closed receipt/artifact checks, bounded
  process reaping and preservation of existing evidence on failed preflight.
- Changed C++ cpplint, changed production complexity, Go vet, Python compilation and
  `git diff --check` passed.
- Whole-repository complexity still reports 19 existing offenders; file-size
  reports existing `src/detail/io_poll_backend.cpp` at 509 SLOC. Every offending
  source file matches HEAD byte-for-byte; thresholds remain unchanged. These
  baseline failures are not reported as passing gates.

Build/test logs are retained in `/private/tmp/task162-*.log`; the final focused
logs are `task162-focused-on-final2.log`, `task162-focused-off.log` and
`task162-focused-sanitize-final.log`. Static baseline comparison is recorded in
`task162-baseline-equivalence.log`. Final `check-local` receipts are
`task162-check-local-on-retry.log` and `task162-check-local-off-final.log`.
The TLS-on serial rerun passed after an intermittent existing hook-documentation
shell spotcheck failure; no unrelated source or threshold was changed.
The resumed implementation session verified that the normal and instrumented
matrix source hashes and fixture binary hashes still match the current files.
The library and fixture builds were up to date; owner/repacketization tests passed
again in normal and ASan/UBSan builds, all six runner tests passed again, and
changed C++ cpplint and `git diff --check` passed. Both real-client matrices were
rerun successfully in `build/http3-evidence-resume-executor-permitted`, with both
client and fixture exit statuses zero and capture counts checked against actual
UDP traffic. The initial sandboxed rerun failed at UDP bind, before readiness;
its diagnostics remain in `build/http3-evidence-resume-executor`. The same gate
passed with permitted loopback access without changing source.
BSD, Windows and other nonlocal platform
checks were not executed here and belong to CI/the v3 PR under AGENTS.md.

REQ-007 is covered by both real UDP request matrices. REQ-009 is covered by the
shared semantic route registry, binary echo, concurrency and cancellation paths.
DR-V3-001 is exercised by the owned transport/TLS/HTTP/3-to-exchange composition.
This smoke matrix does not claim full HTTP/3 conformance or public enablement.
