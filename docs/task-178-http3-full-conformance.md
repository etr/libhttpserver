# TASK-178 full HTTP/3 conformance lane

This explicit release gate combines the existing native RFC regressions, two
independent HTTP/3 clients, the independent RFC 9220 WebSocket client, the
TASK-173 QUIC transport matrix, and bounded instrumented fuzz exploration.
`hq-interop` transport success supplies no HTTP/3 or WebSocket evidence.
The inventory is `test/conformance/http3/full-gates.json`; it identifies RFC
9114, RFC 9204 and RFC 9220 categories and supporting tests. These are bounded
implementation coverage checks, not an exhaustive proof of every RFC statement.

Run `./bootstrap`, then configure four distinct out-of-tree builds rooted in
this checkout: TLS-off, TLS-on, ASan/nonrecovering UBSan TLS-off, and the same
sanitizers with TLS-on. All use `CC=clang CXX=clang++`, C++20 and
`--disable-examples`. Sanitized builds require
`CXXFLAGS='-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined'`
and `LDFLAGS='-fsanitize=address,undefined'`. Normal builds use
`CXXFLAGS='-std=c++20 -O1 -g'`. TLS-on configurations set
`V3_TLS_CFLAGS=-I/home/etr/progs/libhttpserver/build/tooling/provider/include`
and `V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'`.
The gate builds required targets in those directories before fresh execution.
Existing build receipts remain available; changed compiler flags require a clean
configuration/build, as with ordinary Automake use.

Provision tools explicitly before invoking the gate. The runner downloads
nothing. Prepare aioquic 1.3.0 using `test/integ/http3-client-requirements.txt`,
quic-go v0.55.0 modules using `test/integ/http3-client-go/go.mod`/`go.sum`, and the
QUIC runner at `740c05a10b61d65e8abd3ad38d60898004d335d9` with its requirements.
The Go adapter rebuild uses `GOPROXY=off GOTOOLCHAIN=local`; all modules must
already be cached. Selected peer and simulator images must already exist.
The current TLS-on endpoint is packaged into the supplied task-owned image tag,
using the existing pinned endpoint base, with Docker networking disabled for
the build. Its immutable image ID and executable hash are checked before use.

A complete task-local invocation from the worktree root is:

```sh
PATH="$PWD/build/tooling/bin:$PATH" \
DOCKER_CONFIG="$PWD/build/tooling/docker" \
GOPATH="$PWD/build/tooling/gopath" GOCACHE="$PWD/build/tooling/gocache" \
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
python3 scripts/run-v3-http3-full-gates.py \
  --output build/task178-full-next \
  --tls-off-build build/task178-off --tls-on-build build/task178-on \
  --sanitized-off-build build/task178-san-off \
  --sanitized-on-build build/task178-san-on \
  --compiler /usr/bin/clang++ --python "$PWD/build/http3-venv/bin/python" \
  --go "$PWD/build/tooling/go/bin/go" --go-client "$PWD/build/http3-client-go" \
  --tshark "$PWD/build/tooling/bin/tshark" \
  --runner-python "$PWD/build/http3-venv/bin/python" \
  --runner-dir build/tooling/runner \
  --provider-prefix /home/etr/progs/libhttpserver/build/tooling/provider \
  --server-image libhttpserver-quic-task178:current \
  --docker-direct-routing-compat
```

`make check-v3-http3-full FULL_HTTP3_ARGS='...'` exposes the same explicit lane.
Missing arguments, clients, required native summaries, captures, process exits,
closure witnesses, fuzz instrumentation, or receipt hashes fail the lane.
The dependency-free `http3_full_gate_runner_test.py` is registered in ordinary
Automake test execution; it does not require Docker or independent clients.
Choose a fresh output path for every attempt. The QUIC runner invokes bare
`tshark` and `editcap`; the declared decoder must match PATH, and the key-log
injection helper must be available before starting the matrix.

The private output directory retains `manifest.json`, inventory/source hashes,
compiler and tool identities, provider hashes, exact command arrays, duration,
exit status, native counts, corpus and binary hashes, and child runner receipts.
A failed stage preserves earlier results. Existing output directories are
rejected intact. Do not commit certificates, TLS keys, captures or build output.
TASK-173 simulator captures retain their network topology; TASK-174/175 captures
are actual application-boundary datagrams in synthesized IPv4/UDP envelopes.

The QUIC exclusions are copied verbatim from TASK-173. Extreme-loss 50-connection
multiconnect and IP-address rebinding are outside that bounded matrix; port
rebinding remains required for both selected peers. Transfer success requires
an additional authenticated zero-error close witness. The observed aioquic
rebinding limitation does not waive the selected peers' coverage. 0-RTT is
disabled; WebSocket extensions are refused. Baseline framing, critical stream
errors, compression limits, cancellation, trailers, GOAWAY and RFC 9220
negotiation remain required.

Fuzz targets are `quic_parser quic_state quic_recovery http3_engine qpack
websocket_codec` with TLS off and `quic_tls_callbacks` with TLS on. Each lane
requests 2000 runs with seed 128, a 30-second total ceiling, five-second input
timeout and 512 MiB RSS ceiling. The manifest records actual completed runs.
Project sources are compiled directly with libFuzzer/ASan/UBSan; ordinary
archives do not satisfy that gate. Configured sanitizer builds instrument
library, fixture and test compilation; the reused OpenSSL provider and
independent clients are outside that instrumentation. LeakSanitizer is disabled
in this local environment; deterministic reservation teardown and ASan/UBSan
remain required. No passing leak-detection claim is made.

BSD, Windows, macOS and other nonlocal checks remain deferred to CI/the v3 PR,
per repository policy. A local success cannot mark them passed.

## Historical failed attempts and independent probes

The initial 2026-10-09 Linux/WSL2 attempts **failed**, with TASK-178 In
Progress and no complete aggregate passing receipt at that point. Artifacts
below are relative to the task worktree and remain retained as history. The
independent probes could not substitute for the failed aggregate QUIC stage.
The subsequent repair and passing aggregate are recorded at the end of this
document.

| Evidence | Observed result | Artifact directory |
|---|---|---|
| Normal TLS-off native lane | 48 suites, 532 cases, zero failures/skips | `build/task178-full-04/native-off` |
| Normal TLS-on native lane | 55 suites, 597 cases, zero failures/skips | `build/task178-full-04/native-on` |
| ASan/UBSan TLS-off native lane | 48 suites, 532 cases, zero failures/skips | `build/task178-full-04/sanitized-off` |
| ASan/UBSan TLS-on native lane | 55 suites, 597 cases, zero failures/skips | `build/task178-full-04/sanitized-on` |
| TASK-173 QUIC matrix | 10/12 cells passed; quic-go handshake and rebind-port failed | `build/task178-full-04/quic-interop/evidence` |
| Independent TASK-174 HTTP/3 probe | aioquic and quic-go each passed 11 scenarios, including parity/slow peers | `build/task178-http3-probe` |
| Independent RFC 9220 probe | aioquic passed 11 scenarios | `build/task178-websocket-probe` |
| Instrumented TLS-off fuzz probe | All six targets completed 2000 runs each, without sanitizer errors | `build/task178-fuzz-off-probe` |
| Instrumented TLS-on callbacks probe | Completed 2000 runs in 4 seconds, without sanitizer errors | `build/task178-fuzz-on-probe` |

Fuzz compiler/capability receipts, exact source compile recipes, writable seed
copies, bounds and outputs are retained in those probe directories; verified
artifact hashes and actual counts are in `build/task178-fuzz-probes.json`.
The TLS-off targets completed in 0/1/1/1/0/0 seconds in the listed target order.
The libFuzzer one-run capability invocation reports two initialized inputs;
acceptance checks positive completion with no sanitizer error. Bash `%q` compile
receipts are parsed as arguments before checking instrumentation, without
executing their text.

The standalone unchanged TASK-173 runner also failed quic-go handshake at
`build/task178-quic-probe-04` (11/12 cells passed). In the aggregate attempt's
named quic-go handshake captures, the simulator left interface recorded ten
client Initial datagrams, while its right interface recorded no QUIC traffic.
The endpoint's application-boundary capture contains only the simulator
readiness probe/reply. The client timed out and the upstream predicate reported
`Expected exactly one version. Got []`. This required wire/handshake proof is
missing. The separate rebind-port failure reported that the first server packet
on a new path lacked PATH_CHALLENGE. Its cause remains unverified; no required
predicate, timeout or exclusion was changed. Ownership triage belongs to the
TASK-173 transport/simulator maintainers; a valid current-source twelve-cell
matrix is the blocked dependency for TASK-178 acceptance.

Earlier attempts are retained at `build/task178-full-01` through
`build/task178-full-03`. They include a corrected native corpus-summary adapter
failure, an interrupted run and failures before the missing editcap helper was
restored. No old receipt is treated as current passing evidence. Reproduction
and investigation notes are `/tmp/task178-debug-journal.md`.

Exact tool identities observed in attempt04 are clang 18.1.3 (Ubuntu
1ubuntu1), Python 3.12.3, Go 1.25.1, aioquic 1.3.0, HTTP/3 quic-go v0.55.0,
OpenSSL 3.5.9 (2026-09-29), TShark/Editcap 4.6.4, Docker 29.1.3 and Compose
2.40.3+ds1-0ubuntu1. The independent transport peers are quic-go commit
`431fe2e3946b8d75c6db436a6866a83a91a24a7d` and picoquic
`1.1.47.0 / 7cafedfe53695357be230dec36a39952bf447898`.
The runner pin, peer/simulator digests and immutable image IDs are retained in
the QUIC manifest; the source-built endpoint image ID is
`sha256:79135c88906dc88461c7c15f49a42dcdd5ba2f495af2633d2b394aaa302dbcaa`.
Provider archive/header hashes and compiler/configuration receipts are in the
aggregate manifest and build directories. These identities describe attempted
verification, not a release approval.

The dependency-free aggregate contract has 25 passing tests. Source-distribution
inclusion and public-header hygiene passed. The existing file-size check still
fails on unchanged `src/detail/io_poll_backend.cpp` (509 SLOC, limit 500);
its threshold was preserved.
The registered contract also passed through Automake using
`make -C build/task178-off/test check-TESTS check_PROGRAMS= TESTS=integ/http3_full_gate_runner_test.py`;
the separate native lanes above provide the required C++ executable evidence.

## Historical resumed verification (2026-10-09)

Fresh aggregate attempt `build/task178-full-05/manifest.json` again failed the
required TASK-173 matrix. All four native stages passed: TLS-off and sanitized
TLS-off each ran 48 suites/532 cases; TLS-on and sanitized TLS-on each ran 55
suites/597 cases. Each selected native stage had zero failures and skips.
The matrix passed 10/12 cells: quic-go handshake failed with
`Expected exactly one version. Got []`, and picoquic rebind-port failed with
`PATH_CHALLENGE without a PATH_RESPONSE`. The quic-go rebind-port cell passed
in this attempt. Both failures and successful cells remain recorded in
`build/task178-full-05/quic-interop/evidence/results.json`; child cleanup exited
zero, and aggregate execution exited one. Later aggregate stages were not run.

The named quic-go handshake capture again contains ten 1280-byte Initial
datagrams at the simulator's left interface and no UDP at its right interface.
The endpoint reported only the readiness probe/reply. Comparing the earlier
passing transfer capture shows 1280-byte client packets forwarded normally;
packet size alone does not explain this missing forwarding. Read-only inspection
of the pinned startup scripts found no verified cause. No client, simulator,
transport source, required predicate, timeout or exclusion was changed.
At that point, TASK-173 transport/simulator ownership triage remained the
blocked dependency; a valid current-source twelve-cell receipt was still
required for TASK-178.

The aggregate, HTTP/3, WebSocket and fuzz runner contracts passed again (25,
21, 12 and 10 tests respectively). The QUIC runner contracts passed 25 tests;
four opt-in integration tests were skipped in that dependency-free invocation.
The registered aggregate Automake test passed 1/1 with zero skips/errors;
its log is `/tmp/task178-resume-automake-contract.log`. Python compilation and
`git diff --check` passed. The task card and index remain In Progress, and all
prepared changes remain unstaged for the runner. Original evidence was retained.

## Local repair and passing aggregate (2026-10-09)

The current passing aggregate receipt is
`build/task178-repair-full-02/manifest.json`. All nine required local stages
passed in one execution. Earlier failed receipts remain retained as history.

| Required gate | Current result |
|---|---|
| C++20 TLS-off / TLS-on | 48 suites / 532 cases and 55 suites / 597 cases; no failures or skips |
| ASan/nonrecovering UBSan TLS-off / TLS-on | The same 48 / 55 suites and 532 / 597 cases; no failures or skips |
| TASK-173 QUIC | All 12 cells passed, with both authenticated zero-error closure witnesses and cleanup exit zero |
| TASK-174 HTTP/3 | aioquic and quic-go each passed all 11 scenarios |
| RFC 9220 WebSocket | aioquic passed all 11 scenarios |
| Instrumented fuzz | All six TLS-off targets and the TLS-on callbacks target completed 2000 runs each without sanitizer errors |

Before repair, a standalone unchanged QUIC matrix passed all twelve cells at
`build/task178-repair-quic-01`, but aggregate reproduction
`build/task178-repair-full-01` failed quic-go transferloss and picoquic keyupdate.
The transferloss endpoint saw only readiness traffic. A focused diagnostic then
recorded the simulator scenario exiting with status zero before client readiness
at `build/task178-simulator-diag-06`; the client timed out and no handshake was
forwarded. The exact internal reason for that premature exit was not established.

The task-local driver now explicitly selects `ns3::RealtimeSimulatorImpl` with
`NS_GLOBAL_VALUE` during process initialization, before scenario code can select
a simulator. The pinned simulator image, scenario parameters, peers, predicates,
timeouts and exclusions are preserved. The QUIC manifest records this setting
as `simulator_global_values`. Ten focused handshake reruns with the setting
passed at `build/task178-simulator-diag-07` through `-16`, followed by the complete
passing aggregate above. Diagnostic wrappers were used only for investigation;
the aggregate used the pinned simulator's original entrypoint.

The setting regression failed before the repair and passed afterward. The QUIC
runner contracts passed 26 dependency-free tests (four existing opt-in tests
were skipped), and the aggregate contracts passed 25 tests. All changes remain
unstaged in the task worktree for the runner-owned commit flow. TASK-178 remains
In Progress pending the normal runner lifecycle. Nonlocal platforms remain
deferred to CI/the v3 PR; LeakSanitizer remains disabled as described above.
