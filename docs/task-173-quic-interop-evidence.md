# TASK-173 required QUIC interoperability matrix

Implementation is in the isolated `task/TASK-173` worktree based on `v3`
(`502164e96f6bf3d17405f358fdd91c26bfa16328`). Finalization remains coordinator-owned.

The bounded driver selects the explicit `--matrix task173` configuration in
`test/interop/quic/task173-matrix.json`. The default invocation preserves the
TASK-163 quic-go handshake/transfer matrix. The owned native test endpoint
negotiates `hq-interop` and serves HTTP/0.9 files: these receipts prove QUIC
transport interoperability, while the focused native h3 owner regression suite
checks HTTP/3 composition separately. No production listener or public API is
introduced.

## Cases and unchanged upstream predicates

| Acceptance category | Upstream case | Evidence predicate |
|---|---|---|
| Handshake | `handshake` | One handshake, expected QUIC version and file integrity |
| Loss | `transferloss` | 2% bidirectional burst loss, one handshake and byte-for-byte 2 MiB transfer |
| Rebinding | `rebind-port` | 10 MiB transfer, multiple paths, first server packet on each new path contains PATH_CHALLENGE, matching client PATH_RESPONSE |
| Amplification | `amplificationlimit` | Nine-certificate chain, dropped client retransmissions, handshake CRYPTO extent at least 7500 bytes and upstream amplification accounting |
| Key update | `keyupdate` | 3 MiB transfer and authenticated phase-1 packets from both peers |
| Closure | `transfer` plus task-local packet witness | 2/3/5 MiB file integrity plus decrypted client normal CONNECTION_CLOSE and no subsequent server STREAM frames |

The upstream transfer description mentions closure, but its actual `check()`
checks handshake count, version and file integrity only. The driver therefore
requires an additional packet witness for each peer; transfer success alone
cannot satisfy closure. The witness uses the server-side simulator capture and
test-only NSS secrets, retains the exact tshark command and decoded TSV, and
fails on absent closure, unexpected errors, malformed rows or post-close STREAM.
The required peers use zero-error closure; their expected normal application
code is explicit in the pinned matrix, and transport errors must be zero.
The adapter also supports an explicitly configured normal application code,
covered by a regression for aioquic H3_NO_ERROR (0x100).
The native owner lifecycle tests separately cover draining and terminal disposal.

Upstream amplification accounting allows a 3–4x tolerance. Its predicate is
preserved unchanged; the native amplification regression suite independently
checks the strict 3x accounting invariant.

All selected upstream server testnames are already supported: `handshake` or
`transfer`, with no SERVER_PARAMS. No endpoint wrapper expansion was necessary.
The new direct-dispatch tests check both names, rejection of parameters, client
role and unsupported-case rejection, and explicit diagnostics mode.

The `handshakeloss` 50-connection extreme-loss case and IP-address rebinding are
explicitly excluded: loss uses `transferloss`, rebinding uses `rebind-port`.
They do not remove any of the six required categories.

## Pinned inputs and replay

- Runner: `https://github.com/quic-interop/quic-interop-runner`, clean commit
  `740c05a10b61d65e8abd3ad38d60898004d335d9`.
- quic-go: `martenseemann/quic-go-interop@sha256:6743eea41cb8d525686eb8f2437e3ac45835740ec87c62728f645cd89e85ab88`,
  runtime source revision `431fe2e3946b8d75c6db436a6866a83a91a24a7d`.
- picoquic: `privateoctopus/picoquic@sha256:7e4110e3260cd9d4f815ad63ca1d93e020e94d3a8d3cb6cb9cc5c59d97999b05`,
  runtime 1.1.47.0 / source revision
  `7cafedfe53695357be230dec36a39952bf447898`. Its shipped entrypoint selects
  hq-interop for the required transfer/handshake cases and initiates key update
  with `-u 32`. The image entrypoint, executable hash and source revision are
  retained in the evidence directory.
- Simulator: `martenseemann/quic-network-simulator@sha256:c23d82a55caffe681b1bdae65d4d30d23e1283141a414a7f02ee56cf15f9c6b9`.
- Endpoint base: `martenseemann/quic-network-simulator-endpoint@sha256:3b2b9e6fa317da238c8140a7b6d2bf8d4d45a8464b873eecac3ff2a32520b71a`.
- OpenSSL 3.5.9 archive SHA256:
  `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`.
- Tools: Clang 18, Docker 29.1.3, Compose 2.40.3, tshark 4.6.4, Python 3.12.
  Exact package/tool outputs and endpoint SHA256 are retained in the manifest.

Provision the pinned runner and its requirements in a task-local venv, Docker,
Compose, compatible tshark and the checksum-verified provider archive first.
Compose and the tshark runtime were extracted from the existing
`task163-runner-tools:local` image into ignored `build/tooling/`. No host package
or daemon configuration changed. The optional compatibility flag changes only
the copied runner's two task-owned networks, as documented in TASK-163.

```sh
bash bootstrap
mkdir -p build/task173-clang-on
cd build/task173-clang-on
../../configure --enable-v3-tls --disable-examples CC=clang CXX=clang++ \
  CXXFLAGS='-O1 -g' \
  V3_TLS_CFLAGS='-I/home/etr/progs/libhttpserver/build/tooling/provider/include' \
  V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'
make -C src -j4 libhttpserver_v3core.la
make -C test -j4 quic_interop_endpoint quic_interop_files quic_interop_tls \
  http3_network_owner quic_key_state quic_amplification quic_connection_identity \
  quic_connection_lifecycle quic_recovery quic_repacketize quic_datagram_sizing \
  quic_server_admission quic_tls_handshake quic_recovery_tls
cd test
for suite in quic_interop_files quic_interop_tls http3_network_owner \
  quic_key_state quic_amplification quic_connection_identity \
  quic_connection_lifecycle quic_recovery quic_repacketize quic_datagram_sizing \
  quic_server_admission quic_tls_handshake quic_recovery_tls; do
  ./$suite || exit 1
done
cd ../../..
QUIC_INTEROP_ENDPOINT="$PWD/build/task173-clang-on/test/quic_interop_endpoint" \
  python3 -m unittest discover -s test/integ -p quic_interop_runner_test.py
docker build -f test/interop/quic/Dockerfile -t libhttpserver-quic-task173:source .
PATH="$PWD/build/tooling/bin:$PATH" DOCKER_CONFIG="$PWD/build/tooling/docker" \
  build/tooling/runner-venv/bin/python scripts/run-v3-quic-interop.py \
  --runner-dir build/tooling/runner --output build/task173-interop-replay \
  --server-image libhttpserver-quic-task173:source --matrix task173 \
  --docker-direct-routing-compat
```

Use a fresh output directory for every invocation. Existing evidence directories
are rejected before provisioning or mutation. Required cells, peer identities,
independent stacks, both simulator captures, supplementary endpoint capture,
readiness, valid NSS secrets and restrictive permissions gate acceptance.
Failed/timed-out runs retain available logs, results, input provenance and primary
failure details even if Compose cleanup fails. The 1200-second overall deadline
and upstream per-case deadlines remain finite.

Captures, secrets and bulky outputs stay under ignored `build/`; secrets are not
committed. Simulator captures are primary network evidence. The endpoint capture
is supplementary application-boundary output, labelled separately.

## Local regression evidence

`build/task173-red-matrix.log`, `task173-red-execution.log` and
`task173-red-identities.log` retain failing-before-implementation contract tests.
`task173-native-contracts.log` records all 29 Python contracts passing with the
freshly built endpoint and zero skips. Process fakes test orchestration only.

`quic_interop_files` exposed a stale baseline test: it expected one 8192-byte
packet despite TASK-165's conservative 1200-byte datagram cap. Its corrected
regression drains the first retained chunk through bounded repacketization,
verifies contiguous offsets and no premature FIN. The passing correction to
that stale assertion is retained separately.

The final test-only sender retains up to eight independently owned 8 KiB chunks
per stream: at most 64 KiB per stream, within the existing connection-wide
256 KiB cap and its critical-data reserve. A completion frees only its matching
slot, including out-of-order completions; cancellation cancels every occupied
slot. Unknown or zero completion IDs cannot free a slot. No ninth chunk is
read until a completion frees capacity.

The single-chunk rerun passed both peers' multiplexed transfer cases but failed
both rebinding cases. Its quic-go trace stops sending on the original mapping
just before the third change at 11 seconds: all its eliciting traffic had been
acknowledged, and it had no packet to send after translation. The server cannot
discover a mapping without an authenticated client packet. A bounded rolling
pipeline supplies normal continuous file output while retaining original 8 KiB
record granularity. A single 64 KiB record experiment passed one matrix but also
produced intermittent multiplexed-transfer stalls; that record size is excluded
from the final sender.

`task173-red-file-pipeline.log` / `task173-green-file-pipeline.log` verify the
64 KiB per-stream ceiling despite 128 KiB flow credit and a larger connection
budget, eight separate completion receipts, reversed completion order, and
refill at the correct offset. `task173-red-pipeline-identity.log` verifies zero
and unknown completion IDs are rejected. All other endpoint bounds remain
unchanged, including the 16 MiB file cap and 8 KiB read buffer.

Compile logs explicitly show `clang++ -std=c++20`. TLS-dependent tests must run
from `build/task173-clang-on/test`, where their compiled relative fixture paths
resolve. BSD, Windows and other nonlocal checks remain assigned to CI/v3 PR.

The thirteen focused native suites passed with zero skips: files/TLS, h3 owner,
key state, amplification, identity, lifecycle, recovery, repacketization,
datagram sizing, server admission, TLS handshake and recovery/TLS composition:
151 tests / 8597 checks, zero failures or skips. The exact per-suite counts are
in `build/task173-focused-checks.json`; their compiler/test logs remain beside it.
Changed C++ cpplint, Python compilation, `check-local` and distribution inclusion
of the matrix and evidence document passed. Doxygen was unavailable and its
existing `check-local` gate reported its standard CI skip. The independent
Clang C++20 TLS-off build passed seven focused suites: 85 tests / 1397 checks,
zero failures or skips, recorded in `build/task173-off-focused-checks.json`.
Its `check-local` also passed. BSD/Windows and other nonlocal checks were not
executed locally and remain assigned to CI/v3 PR.

## Demonstrated CID/path repairs

Both the original 8 KiB capture and the 64 KiB diagnostic run contain
authenticated quic-go packets on new source ports. The server sent no
PATH_CHALLENGE. In `task173-interop-attempt-02`, packets 53–55 arrived on
translated port 57607 using the advertised destination CID.
Its active path remembered only the originally admitted server CID, while the
client was using an advertised alias. With quic-go's zero-length client CID,
there was no unused peer CID to choose, and the NAT fallback incorrectly rejected
the changed path because its remembered incoming CID was stale.

The production correction is one assignment: the newest authenticated
nonprobing packet records its destination CID on its path, using the existing
packet-number ordering guard. Reordered packets cannot overwrite this latest
identity. Discovery, policy, amplification accounting and validation remain
required before activation.

`task173-red-identity-unit.log` / `task173-green-identity-unit.log` verify alias
rotation followed by an older packet and then NAT discovery. The real hq
composition regression uses a zero-length peer CID, negotiates hq-interop,
rotates to an issued alias, completes both bounded path validation probes and
serves a file over the new endpoint. Its failing/passing receipts are
`task173-red-rotated-cid-rebinding.log` and
`task173-green-rotated-cid-rebinding.log`. Existing h3 owner regressions and the
recovery/TLS suite pass after the correction.

The endpoint was freshly recompiled and relinked from the immutable
source-build stage `e9087eade4f9f513239980cae0905161706e6f982e2d4a3e7826fd60a4ea941e`
using task-local cached-stage build recipes. The final recipe is
`build/Dockerfile.task173-final`, including both native repairs and the final
bounded file pump source and header. Unchanged object files and the static
provider were reused from the task-owned cache. The exact local build recipe,
build output and hashes of the executable, provider archives and changed native
sources are retained with each corrected-image receipt as `server-build.Dockerfile`,
`server-build.log` and `server-build-provenance.sha256`. A clean replay can use
the repository's source Dockerfile shown above; no TASK-163 executable is reused.

## Preserved failed attempts and peer selection

The preferred aioquic image was tried genuinely at immutable digest
`aiortc/aioquic-qns@sha256:abc8469ba168a7f2183870d9437f2815487a8c9068aa3b79eb70b3ded4315e14`,
shipping runtime 1.3.0. It passed handshake, transfer, loss, amplification and
key update, but did not finish rebinding. Each required category remains mandatory
for both selected peers; picoquic replaces aioquic rather than waiving rebinding.

| Evidence directory under `build/` | Endpoint state | Observed outcome |
|---|---|---|
| `task173-interop-attempt-01` | Original 8 KiB window | Both rebinding cells failed; original Compose cleanup interpolation also failed, with primary results preserved |
| `task173-interop-attempt-02` | Original 8 KiB, corrected cleanup | Both rebinding cells failed; cleanup succeeded |
| `task173-interop-window-01` | 64 KiB, before CID repair | Both rebinding cells failed; authenticated quic-go packets on new ports identified the stale path CID |
| `task173-interop-cid-fixed-01` | 64 KiB plus CID repair | All six quic-go cells succeeded; aioquic rebinding timed out while its other five cells succeeded |
| `task173-interop-picoquic-01` | 64 KiB plus CID repair | Both peers passed five cells but stalled in rebinding; quic-go stopped after about 5 MiB |
| `task173-interop-final` | Single 64 KiB record plus both repairs, before PTO-grant refinement | Both rebinding cells passed; both multiplexed transfer cells failed, so this repeat was not accepted |
| `task173-interop-evidence-final` | Original single 8 KiB record plus both repairs | Both multiplexed transfers passed but both rebinding cells failed; capture identified the remaining continuous-output requirement |

The corrected-image aioquic server-side simulator trace contains client packets
only through 0.986 seconds, all on the original translated source port; the
unchanged simulator switches that mapping at one second. No authenticated client
packet reaches the server on a new port, so the server cannot discover it. The
remaining stalled server retransmissions target the old mapping. This observed
peer limitation is recorded explicitly in the matrix exclusions. Upstream
scenarios, success predicates and deadlines were preserved throughout.

The picoquic selection run exposed a second native problem. During quic-go's
third rebinding the small PATH_CHALLENGE and matching response reached the new
port, but no padded validation challenge followed. The old path's stranded
flight consumed the shared congestion window; the candidate's validation packet
was incorrectly gated by that old window while its application ACKs were
withheld until validation. This circular wait was independent of the CID repair.

Candidate validation probes now use a separate aggregate initial window of
12,000 bytes, shared by all candidate controls. Old-path flight cannot block
those probes. The existing sent-record capacity, per-path amplification budget,
three challenge attempts, packet-size cap and write-key/send-permission gates
still apply. Application data and active-path controls keep their existing
congestion and pacing gates. This follows the path isolation and probing
provisions in [RFC 9000 section 9.4](https://www.rfc-editor.org/rfc/rfc9000.html#section-9.4).

`task173-red-candidate-window.log` / `task173-green-candidate-window.log`
verify that ten 1200-byte candidate probes can progress with a full old-path
window, that an eleventh is blocked, that active-path probes remain blocked,
and that send/key/PTO-grant gates still reject candidate output.
`task173-red-probe-grant.log` / `task173-green-probe-grant.log` specifically
cover a requested PTO probe without a grant; the final implementation reuses the
existing permission gate rather than duplicating it. The protected hq
regression additionally strands old-path file output, discards all traffic to
the old NAT mapping, validates the new port and verifies all three 64 KiB files
and their FINs arrive there with bounded 8 KiB chunk ownership. Its regression
receipts are
`task173-red-full-flight-rebinding.log` and
`task173-green-full-flight-rebinding.log`.

## Required matrix receipts

The final receipt is
`build/task173-interop-final-receipt/manifest.json`: `passed: true`, upstream
return code 0, cleanup return code 0, all twelve cells succeeded and all
36 required captures passed structural checks. All recorded source hashes match
the final worktree inputs. The freshly linked endpoint SHA256 is
`4ee3f7b0a42d6cb5226f63028da81a1188bcad7352c2b6ebdeb8235fd71511a6`.
The unchanged final executable hash is expected after the last header-comment
correction; the final image includes that header and the manifest records it.

| Independent peer | Handshake | Loss | Rebinding | Amplification | Key update | Closure |
|---|---|---|---|---|---|---|
| quic-go | PASS | PASS | PASS | PASS | PASS | PASS |
| picoquic | PASS | PASS | PASS | PASS | PASS | PASS |

The closure witnesses contain authenticated client CONNECTION_CLOSE with error
code zero at frame 22686 for quic-go and frame 21338 for picoquic, with no server
STREAM after closure. Their decoded TSVs and exact commands are retained below
`runner-logs/libhttpserver_<peer>/transfer/`. `results.json`, `stdout.log`,
`stderr.log`, `server-build.Dockerfile`, `server-build.log`,
`server-build-provenance.sha256` and picoquic runtime/revision/entrypoint logs
are retained beside the manifest. The outer invocation log is
`build/task173-interop-final-receipt-driver.log`.

`build/task173-interop-probe-fixed-01/manifest.json` records `passed: true`,
all twelve unchanged upstream cells succeeded, both peer closure witnesses
passed, and Compose cleanup returned zero. This experimental-window receipt is
preserved separately from the bounded-pipeline receipt at
`build/task173-interop-pipeline-final/manifest.json`, which also records all twelve
upstream cells passing, two authenticated normal closure witnesses and successful
cleanup. Its quic-go closure is frame 22654 and picoquic closure is frame 21175.
The intervening repeat's failed transfer cells are preserved and documented above.

The final source-built endpoint and production source hashes are recorded in
`server-build-provenance.sha256` beside the final manifest. Replays preserve
every earlier failed attempt. No changed upstream success predicate, mock peer
or substituted platform check contributes to acceptance.
