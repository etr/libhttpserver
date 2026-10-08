# TASK-163 QUIC interop endpoint evidence

Local acceptance passed on Linux on 2026-10-08 from the isolated `task/TASK-163`
worktree based on `v3` (`4f185a86`). The unchanged upstream runner reported
`handshake=succeeded` and `transfer=succeeded` against the owned native endpoint,
then a fresh replay reported both successful cells again. No external server was
substituted. Task completion and merge are tracked in the task card and the
authoritative task index; validation/finalization remain caller-owned.

## Scope and isolation

`quic_interop_endpoint` is a noninstalled test executable. Its separately compiled
`libquic_interop_provider.a` selects exact `hq-interop` ALPN and NSS key export
only when `LIBHTTPSERVER_INTEROP_TEST_MODE=1`. The ordinary provider remains
h3-only: its archive contains neither `hq-interop` nor `SSLKEYLOGFILE`; the normal
QUIC handshake rejection tests and original h3 gate pass. No installed header,
production listener, flow/recovery algorithm, or public API changed.

The runner uses HTTP/0.9 `GET /<file>\r\n`, independently of HTTP/3 framing. The
adapter serves single-leaf regular files beneath `/www`, rejects traversal and
symlinks, consumes request credit, and retains at most one 8192-byte chunk per
stream until its ACK completion. The endpoint bounds files at 16 MiB, requests at
4096 bytes, connections at four, streams at 64, lifetime at 120 seconds, captures
at 64 MiB, and NSS output at 1 MiB. Keys use mode 0600 and evidence roots mode
0700. Unsupported roles/cases return 127. Test mode gates diagnostics and bind;
stdin closure leaves the server alive until a signal or its finite deadline.

Only test composition adapts the runner's EC PARAMETERS PEM prefix, its exact
reserved WAIT readiness probe, and client-chosen Initial CID widths 8–20. The
existing key-state response API answers authenticated peer key updates. The
send pump samples its preparation clock after actual send-completion commit so
pacing receives monotonic time. Existing core transport policies are unchanged.

## Pinned inputs and replay

- Runner: `https://github.com/quic-interop/quic-interop-runner`, commit
  `740c05a10b61d65e8abd3ad38d60898004d335d9` (clean upstream checkout).
- Client: `martenseemann/quic-go-interop@sha256:6743eea41cb8d525686eb8f2437e3ac45835740ec87c62728f645cd89e85ab88`;
  runtime reports quic-go commit `431fe2e3946b8d75c6db436a6866a83a91a24a7d`.
- Simulator: `martenseemann/quic-network-simulator@sha256:c23d82a55caffe681b1bdae65d4d30d23e1283141a414a7f02ee56cf15f9c6b9`.
- Endpoint base: `martenseemann/quic-network-simulator-endpoint@sha256:3b2b9e6fa317da238c8140a7b6d2bf8d4d45a8464b873eecac3ff2a32520b71a`.
- OpenSSL 3.5.9 static provider, archive SHA256
  `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`.
- Native acceptance binary SHA256:
  `e957e742ec03396824775974a8fc363c61a450157c2421ad9b0192d5c9f7cd2b`.
- Native replay image ID:
  `sha256:361c2cc741fae613626cc111f925627a8998d04b99df9410166649ea04716d20`.

Provision a clean pinned upstream checkout and its requirements in a venv,
Docker with Compose, and tshark 4.5+ before invoking the driver. This execution
used Clang 18, Docker 29.1.3, Compose 2.40.3, tshark 4.6.4, and Python 3.12.
Resolved Python packages and source/image/binary identities are in the manifest.
Tooling and provider installation stayed beneath ignored `build/tooling`; no
host package or daemon/firewall policy changed. The tool container's Compose
binary and tshark runtime were extracted for host invocation through supported
Docker permissions, without a Docker socket mount into a container.

Native build (from this worktree, using an already checksum-verified provider):

```sh
bash bootstrap
mkdir -p build/task163-clang-on
cd build/task163-clang-on
../../configure --enable-v3-tls --disable-examples CC=clang CXX=clang++ \
  CXXFLAGS='-O1 -g' V3_TLS_CFLAGS="-I$PWD/../tooling/provider/include" \
  V3_TLS_LIBS="-L$PWD/../tooling/provider/lib -lssl -lcrypto"
make -C src -j4 libhttpserver_v3core.la
make -C test -j4 quic_interop_endpoint quic_interop_files quic_interop_tls
cd ../..
docker build -f test/interop/quic/Dockerfile.native \
  -t libhttpserver-quic-task163:verified .
PATH="$PWD/build/tooling/tshark-runtime/bin:$PATH" \
  build/tooling/runner-venv/bin/python scripts/run-v3-quic-interop.py \
  --runner-dir build/tooling/runner --output build/interop-evidence-verified \
  --server-image libhttpserver-quic-task163:verified --docker-direct-routing-compat
```

The full source-build Dockerfile consumes the same locally provisioned OpenSSL
archive and builds the C++20 core/provider/endpoint in its build stage:

```sh
docker build -f test/interop/quic/Dockerfile \
  -t libhttpserver-quic-task163:source-verified .
```

The native Dockerfile explicitly packages the verified native binary; the source
Dockerfile performs a fresh build. Neither bakes credentials or runner files.
Both export `/provenance.sha256`. The final source image built successfully as
`sha256:9a3dd4740b9e56ecdcbe21806d5d871f3f43a04420d56ef543f4c3f6904810b1`;
its endpoint SHA256 is
`ce6392d13bd1821d483f2788b30de5c5cf2b6c75c3aa9f27d8ef9f8e8d2b6b56`.
`task163-checks/source-image-provenance.log` records its provider archive hashes
and runtime dependencies (libstdc++, libm, libgcc and libc). The no-network
container's disabled-mode probe returned 1 before endpoint startup. Upstream
acceptance receipts above specifically identify the native packaging image.

The optional compatibility switch changes only the two task-owned runner bridge
networks to IPv4/IPv6 `nat-unprotected`. Docker 29's default filtering dropped
simulator-routed client UDP before it reached the endpoint. A bounded packet
probe established that this network option restores delivery. No host daemon,
firewall, or unrelated network was modified. This option permits direct traffic
on these isolated networks; omit it where the original runner network works.
See [Docker bridge gateway modes](https://docs.docker.com/engine/network/port-publishing/#gateway-modes).

## Genuine upstream receipts

Evidence remains beneath this worktree's ignored `build/`:

| Receipt | handshake | transfer | Proof |
|---|---|---|---|
| `interop-evidence-monotonic/results.json` | succeeded | succeeded | First corrected native endpoint run |
| `interop-evidence-verified/results.json` | succeeded | succeeded | Fresh replay with source/tool/binary manifest |

The replay lasted 50.62 seconds total. `manifest.json` reports `passed: true`,
image IDs, exact named runner command, source hashes, tool identities, artifact
sizes/hashes, and capture record counts. Upstream case predicates were unchanged:
the transfer checked the downloaded 2, 3 and 5 MiB files byte for byte and clean
connection closure. Its NSS keys decrypted the simulator traffic.

Simulator captures are kernel pcapng files despite their `.pcap` suffix. Replay
handshake has 35 right-side and 47 left-side records; transfer has 27159 and
27176. Supplementary endpoint `server/packets.pcap` is synthesized at the
application boundary with actual peer/local IPv4 addresses and successful send
accounting: 18 handshake and 27128 transfer records. It is labelled separately
from the simulator capture. Both formats are structurally validated, including
truncation, empty records, and interface/snapshot bounds.

`interop-evidence-verified/transfer-witness.tsv` retains tshark-decrypted fields.
Concurrent server streams 0, 4 and 8 carry data at relative times 0.722, 0.725 and
0.728 seconds before any response completes. Client MAX_DATA advances through
983040, 1179648 and 1376256; MAX_STREAM_DATA advances from 655360 to 786432 and
onward on each active stream. Client logs record response FIN ends exactly
2097152 on stream 8, 3145728 on stream 4 and 5242880 on stream 0. The native
4-byte-credit test separately parks one stream, advances a sibling, and resumes
after connection and stream credit updates. The witness command and hashes are
retained in the manifest; key material is never committed.

## RED/GREEN and regression receipts

`build/task163-checks/` preserves task-local build/test/RED logs with a checksum
manifest. Initial hq TLS handshake, missing endpoint behavior, fragmented file
requests/credit progression, and orchestration receipt tests failed before their
implementations and then passed. Real upstream probes additionally exposed:

- EC PARAMETERS prefix refusal, followed by successful normalized credentials.
- Missing reserved readiness response, followed by an exact WAIT-probe reply.
- Variable Initial CID routing and a partial-Initial null-flow crash. The genuine
  native fragmented encrypted Initial regression failed with SIGSEGV before the
  file pump guard, then passed.
- A peer key-update transition rejection, resolved through the existing key-state
  response API, and transfer timeouts when prepare used a clock sampled before
  send completion. `interop-evidence-keyupdate` and `interop-evidence-diag`
  preserve failed upstream cells; the monotonic pump correction produced both
  successful cells in two fresh runs.

Final local checks passed:

- C++20 Clang TLS-on native build; 13 affected suites (hq files/TLS, normal owner,
  QUIC TLS/session/handshake/admission/flow/recovery/repacketization/key state,
  h3 exchange/streaming/cancellation). New suites: five file tests/45 checks and
  three TLS/owner/export-boundary tests/27 checks. Secret export rejects symlinks
  and fails closed at its output size limit.
- TLS-off build and seven relevant suites, including the same file adapter.
- 15 Python runner/endpoint contract tests with the native executable and six
  existing h3 runner tests; Python compilation and changed C++ cpplint.
- Original independent aioquic 1.3.0 and quic-go 0.55 h3 matrices passed after
  the owner changes: `build/http3-evidence-task163-final`.
- TLS-on/off `check-local`, `git diff --check`, and distribution inclusion of
  all 14 new task packaging/support/test/doc files.
- Focused ASan/UBSan on the changed file helper and test owner/provider path,
  with leak detection disabled because this sandbox cannot run LSan under ptrace.
  The unchanged linked core and static provider are not fully sanitizer-instrumented.

The GCC baseline failed in existing native `peer_policy()` name lookup before
this task's behavior changes; Clang C++20 provides the passing local build gate.
BSD, Windows and other nonlocal checks remain deferred to CI under AGENTS.md.
The hq evidence proves only the named handshake/transfer test slice; migration,
Retry scenarios, loss matrices and other future QUIC tasks are outside its scope.
