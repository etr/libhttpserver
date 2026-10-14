# TASK-199 implementation evidence

Implemented in `task/TASK-199`, registered worktree `/home/etr/progs/libhttpserver/.worktrees/TASK-199`, based on `v3`
at `ff83e59653a8657f040975ccf4fe2c58462dbedf`. Implementation is ready for caller-owned
Groundwork validation/finalization. No staging, commit, merge, worktree operation
or TASK-185 runner edit was performed. The [machine receipt](TASK-199-implementation-receipt.json)
and [current report](../../../test/installed/task199-capabilities.md) retain the
specific proof boundary; [the adapter interface](../../../docs/v3-packaged-release-adapters.md)
provides TASK-185's two callable families and independent validators.

## Fresh distribution and installed execution

The frozen integration archive is `/tmp/task199-dist-build4/libhttpserver-3.0.0.tar.gz`, SHA-256
`75cfe738dd0857f4be9c347d9db36c170007c67dc4dbcfb47c8ca9f25b5f50a1`; its extraction has no `.git`. Current executable
adapter/consumer sources match their distributed members. Documentation and
this receipt were reconciled after execution, so the archive does not contain
its own generated evidence. The final distribution check separately verifies
all newly registered files.

Autotools bootstrap/configure and fresh optimized C++20 builds use Clang 18 and
normal allocators: `CXXFLAGS='-O2 -DNDEBUG'`, `--disable-examples`, explicit
`--enable-v3-tls=no|yes`, and the selected OpenSSL provider. Each lane runs
`make -C src -j6`, `make -C src install`, `make install-data-am`; inputs and full
logs are retained under `/tmp/task199-*4*`. TLS-off build/install:
`/tmp/task199-build-off4`, `/tmp/task199-prefix-off4`; TLS-on:
`/tmp/task199-build-on4`, `/tmp/task199-prefix-on4`.

Provider: `/home/etr/progs/libhttpserver/build/tooling/provider`. Genuine clients: hyper-h2 4.4.1,
aioquic 1.3.0, Node v24.15.0, quic-go v0.55.0.
The exact tool/module/provider/header/library/configuration hashes are bound by
outer receipts and rehashed by independent validation. Tool executables and
client modules belong to the immutable artifact catalogs; the independent
provider-only fixture tool is distinguished from installed public servers.

Execution commands:

```sh
/tmp/task197-validation-clients/bin/python /tmp/task199-extract4/libhttpserver-3.0.0/scripts/run-v3-packaged-adapters.py --family public-capabilities --archive /tmp/task199-dist-build4/libhttpserver-3.0.0.tar.gz --archive-sha256 75cfe738dd0857f4be9c347d9db36c170007c67dc4dbcfb47c8ca9f25b5f50a1 --source /tmp/task199-extract4/libhttpserver-3.0.0 --build /tmp/task199-build-off4 --prefix /tmp/task199-prefix-off4 --provider-prefix /home/etr/progs/libhttpserver/build/tooling/provider --tls no --compiler /usr/bin/clang++-18 --client-python /tmp/task197-validation-clients/bin/python --node /tmp/node-v24.15.0-linux-x64/bin/node --quic-go /tmp/task198-go-client --run-id 19900000000000000000000000000031 --output /tmp/task199-evidence4/public-capabilities-no
```

```sh
/tmp/task197-validation-clients/bin/python /tmp/task199-extract4/libhttpserver-3.0.0/scripts/run-v3-packaged-adapters.py --family public-capabilities --archive /tmp/task199-dist-build4/libhttpserver-3.0.0.tar.gz --archive-sha256 75cfe738dd0857f4be9c347d9db36c170007c67dc4dbcfb47c8ca9f25b5f50a1 --source /tmp/task199-extract4/libhttpserver-3.0.0 --build /tmp/task199-build-on4 --prefix /tmp/task199-prefix-on4 --provider-prefix /home/etr/progs/libhttpserver/build/tooling/provider --tls yes --compiler /usr/bin/clang++-18 --client-python /tmp/task197-validation-clients/bin/python --node /tmp/node-v24.15.0-linux-x64/bin/node --quic-go /tmp/task198-go-client --run-id 19900000000000000000000000000032 --output /tmp/task199-evidence4/public-capabilities-yes
```

```sh
/tmp/task197-validation-clients/bin/python /tmp/task199-extract4/libhttpserver-3.0.0/scripts/run-v3-packaged-adapters.py --family native-throughput-latency --archive /tmp/task199-dist-build4/libhttpserver-3.0.0.tar.gz --archive-sha256 75cfe738dd0857f4be9c347d9db36c170007c67dc4dbcfb47c8ca9f25b5f50a1 --source /tmp/task199-extract4/libhttpserver-3.0.0 --build /tmp/task199-build-off4 --prefix /tmp/task199-prefix-off4 --provider-prefix /home/etr/progs/libhttpserver/build/tooling/provider --tls no --compiler /usr/bin/clang++-18 --client-python /tmp/task197-validation-clients/bin/python --node /tmp/node-v24.15.0-linux-x64/bin/node --quic-go /tmp/task198-go-client --run-id 19900000000000000000000000000033 --output /tmp/task199-evidence4/native-throughput-latency-no --requests 16 --warmup 4
```

```sh
/tmp/task197-validation-clients/bin/python /tmp/task199-extract4/libhttpserver-3.0.0/scripts/run-v3-packaged-adapters.py --family native-throughput-latency --archive /tmp/task199-dist-build4/libhttpserver-3.0.0.tar.gz --archive-sha256 75cfe738dd0857f4be9c347d9db36c170007c67dc4dbcfb47c8ca9f25b5f50a1 --source /tmp/task199-extract4/libhttpserver-3.0.0 --build /tmp/task199-build-on4 --prefix /tmp/task199-prefix-on4 --provider-prefix /home/etr/progs/libhttpserver/build/tooling/provider --tls yes --compiler /usr/bin/clang++-18 --client-python /tmp/task197-validation-clients/bin/python --node /tmp/node-v24.15.0-linux-x64/bin/node --quic-go /tmp/task198-go-client --run-id 19900000000000000000000000000034 --output /tmp/task199-evidence4/native-throughput-latency-yes --requests 16 --warmup 4
```


TLS-on public execution runs inside `unshare -Urn` with only loopback enabled,
using authentic TCP443 for ACME. The initial host bind rejection is preserved;
no port substitution or injected metadata supplies proof. Every final family
returns zero and a new-process `--validate OUTPUT/receipt.json` with the same
input/run-ID/workload arguments returns zero. All artifacts, archive inputs,
consumer directories and staged packages remain available for revalidation.

## RED/GREEN and integrity gates

RED preceded adapter implementation, caller run-ID propagation, supported
configuration parsing, installed H1/WS support, copied/private provenance
rejection, and identity/protocol/JSON strengthening. The machine receipt lists
retained `/tmp/task199-red*.log` and matching GREEN logs. Later bounded probes
causally exposed configuration/profile log collisions, a lazy H2 binding missing
from H3 aggregate/semantic execution, HTTP status versus scenario status parsing,
and missing selected-client catalog/source references. Each has a focused
regression and a fresh successful final archive execution. Earlier failed
attempts and their source/install snapshots remain preserved. The temporary
`task199-parser-discriminator-only.json` was a synthetic parsing discriminator;
it is explicitly excluded from accepted integration evidence.

All 138 focused tests pass: installed package (35), installed consumer (28),
package workflow (4), public H2 (6), public H3 (13), benchmark (18), adapters (34).
The dependency-free adapter contracts also pass via Automake's dedicated log
target. Changed C++ files pass cpplint; Python compilation and whitespace checks
pass. No production backend-size cleanup was introduced.

Both fresh builds pass:

```sh
make -C BUILD check-v3-native-linkage check-headers check-hygiene check-v3-installed-package-fixtures
```

Independent strict installed-package audits return no violations, including
unused archive members. Their header sets/effective declaration hashes agree
for all 41 installed headers. Rejection copies are independently rehashed,
so scenario/statistic/process rejection is semantic rather than a stale catalog
hash. The original accepted outputs remain unchanged. Six tampering outcomes
are retained in `/tmp/task199-tampering-final/results.json` and this machine receipt.

## Handoff and ownership

TASK-185 can update from `v3`, import the archive's adapter module, call
`run_public_capabilities(inputs, output, run_id)` and
`run_native_benchmarks(inputs, output, run_id, workload)`, then use the supplied
independent validators with recomputed expected identities. Its exact runner
interface is absent here; TASK-185 owns that final wiring. A reduced benchmark
cannot replace a full performance gate. Historical TASK-179 descriptions,
receipts and unavailable-at-the-time classifications remain unchanged.

TASK-182 is already Complete in both card and index; no status transition was
invented. TASK-199's implementation action items are checked, while its status
remains In Progress for caller validation/finalization. TASK-185 stays open.
BSD/macOS/Windows checks are honestly unexecuted locally and belong to CI/v3 PR.
Existing local sanitizer/resource/release requirements remain unchanged.
