# TASK-147 TCP TLS concurrency evidence

## Implementation boundary

Implementation prepared on `task/TASK-147` in the registered worktree
`/Users/etr/progs/libhttpserver/.worktrees/TASK-147`, based on `v3` at
`6faf159ba26799cb06daa86dbf4fca9afb09504f`. The supplied worktree identity was
verified with `git worktree list --porcelain`, branch, status and commit history.
The identity helper invoked inside this existing worktree treats it as a repository
root and proposes a nested path; the runner's explicit registered path takes
precedence. No branch/worktree lifecycle operations, staging, commits or merges
were performed.

This change adds TCP integration evidence and TLS-only Automake targets. Existing
production TLS behavior passed the new assertions without a production repair.
TASK-147 remains **In Progress** pending caller-owned validation/finalization.

## Requirement mapping and observed behavior

| Requirement / decision | Executable evidence |
| --- | --- |
| PRD-V3N-REQ-034; DR-V3-002/007 | `tls_policy_concurrency`: four certificate publication rounds with TLS 1.2 and 1.3 workers concurrently holding established and parked TCP handshakes; managed poll and local kqueue rotation smoke |
| PRD-V3N-REQ-034 | Old serial **101** / ALPN **h2** and new serial **202** / ALPN **http/1.1**, correlated with each adapter's captured generation; bidirectional application exchange after publication; invalid replacement preserves the exact snapshot and consumes no generation |
| PRD-V3N-REQ-034 | `tls_policy_hostile_profiles`: exact SNI, uppercase/trailing-dot canonical names, unknown/omitted names and suffix lookalikes select the intended certificate and ALPN |
| PRD-V3N-REQ-034 | mTLS `none`, `request`, `require` with absent, trusted, other-root, expired and wrong-purpose clients; failed handshakes publish no metadata; trust-root rotation keeps a parked old-root handshake valid while fresh old-root authentication fails and fresh new-root authentication succeeds |
| PRD-V3N-REQ-035 | Two PSK identities and two hosts with different keys, unknown identity, wrong-host key and throwing callbacks; callback host/generation observations; held old lookup survives replacement, fresh new key authenticates, fresh old key fails, successful peers exchange ordinary bytes |
| PRD-V3N-REQ-035 | Certificate default selects PSK; PSK default selects certificate or required mTLS; PSK cannot bypass required mTLS; certificate and ACME selections do not invoke the discarded PSK lookup |
| PRD-V3N-REQ-036 | Matching canonical SNI and sole `acme-tls/1` expose serial **303** and the expected critical digest extension; ordinary/mixed ALPN exposes serial **101**; unknown/suffix/omitted SNI, unknown transport and actual ephemeral-port metadata obtain no challenge |
| PRD-V3N-REQ-036 | Parked challenge handshakes keep serial **303** across replacement and **404** across removal; fresh replacement uses serial **404** and its different digest; challenge reads/writes fail; ordinary application I/O continues during removal; old snapshots expire after final adapters/completions retire |
| PRD-V3N-REQ-034/035/036 | Failed certificate/key, malformed PEM, incompatible PSK policy, invalid challenge/digest/name and malformed removal preserve the exact active pointer; diagnostics equal the library-owned `TLS credentials invalid`, provider error queues are empty, and the next valid publication advances by exactly one |

Successful ordinary replacement preserves the published challenge, including a
fresh TCP handshake proving its certificate/digest. Removal remains effective
through subsequent ordinary replacement and a fresh challenge attempt fails.
Secret-bearing callback exceptions yield bounded library failure codes without
escaping as exception text. Assertions on diagnostic sentinels use boolean checks
so failure output cannot print the supplied secrets.

## Harness bounds and cleanup

Each connection uses an independent nonblocking OpenSSL socket-BIO peer, an
actual ephemeral loopback TCP listener/accepted socket, `tls_io_backend`, a manual
owner executor and a real poll or kqueue backend. A connection's SSL and executor
are driven only by its owning thread. Workers collect failures and report them
through littletest only after joining.

The forwarding test transport records a submitted real raw read. The server
handshake is outstanding and has submitted that read before the client emits
ClientHello. Publication happens while that handshake is parked. This also works
for managed backends, whose external readiness interests are unavailable. No
production barrier hooks or fake socket completions are used.

Phase/handshake waits have five-second deadlines. Connection driving also has a
twenty-second lifetime budget. The certificate matrix admits at most four active
peers; PSK uses the existing fixed two-worker handshake/lookup lanes, allowing a
new peer to authenticate while one old lookup is held. Abort guards release
callbacks on exceptional exits; socket owners close failed construction paths;
adapters close, real backends release connections, owners drain completions,
workers join and PSK runtimes stop/drain before callback captures are destroyed.
Every focused test invocation used a 180-second subprocess watchdog.

ACME positives inject trusted internal `{tcp, 443}` listener metadata into the
adapter while the OS socket is actually bound to an ephemeral loopback port.
The negative TCP metadata row uses that actual ephemeral port. This proves the
adapter's selection contract, **not** a listener publicly bound to TCP port 443.
ACME peers disable ordinary chain validation for their self-signed certificate
with a critical extension and explicitly inspect serial/digest. Ordinary peers
retain fixture-root chain validation.

## Local verification receipts (2026-10-07, macOS)

Provider: reused immutable `/private/tmp/task129-provider/install`, reporting
**OpenSSL 3.5.9**. Builds use C++20, Homebrew prerequisites, `-O0 -g` for ordinary
builds, and worktree-local VPATH build directories.

| Gate | Result |
| --- | --- |
| Existing focused TLS baseline | **14/14** executables passed, no skips |
| Initial executable RED with barrier scaffold returning incomplete | **6 expected failures**, including the parked-handshake assertion; 7 tests / 26 checks. PSK production behavior already passed |
| Final `tls_policy_concurrency` | **7 tests / 98 checks**, zero failures/skips |
| Final `tls_policy_hostile_profiles` | **6 tests / 474 checks**, zero failures/skips |
| C++20 TLS-on full library build and focused regression run | **16/16** executables passed, zero skips |
| C++20 TLS-off full library build and focused run | **4/4**: `consumer_v3_features`, `io_tls_control`, `io_backend_contract`, `server_options_validate`; zero skips |
| TLS-on/off `check-v3-native-linkage check-local` | Passed in both configurations |
| Changed-file cpplint and `git diff --check` | Passed |
| ASan + UBSan instrumented native sources and new suites | **2/2** executables passed, zero skips/findings, `UBSAN_OPTIONS=halt_on_error=1` |

Initial sandboxed executions denied TCP listener creation, including the existing
`tls_io_loopback` baseline and TLS-off backend contract. The exact affected gates
passed when rerun with permitted local socket access. The fixture also needed
bounded accept readiness and raw-read observation for managed backends; those
were harness repairs, not production invariant failures. Existing bootstrap
Automake warnings and macOS duplicate-library/deprecated `-bind_at_load` linker
warnings remained unchanged.

Build commands use the plan's provider flags:

```sh
./bootstrap
mkdir -p build-on build-off
(cd build-on && ../configure --enable-v3-tls \
  V3_TLS_CFLAGS=-I/private/tmp/task129-provider/install/include \
  V3_TLS_LIBS='-L/private/tmp/task129-provider/install/lib -lssl -lcrypto' \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g')
(cd build-off && ../configure --disable-v3-tls \
  CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib \
  CXXFLAGS='-std=c++20 -O0 -g')
make -C build-on -j2
make -C build-off -j2
```

The TLS-on focused list was:

```text
tls_policy_concurrency tls_policy_hostile_profiles tls_credentials
tls_credentials_rotation tls_selection tls_mtls tls_psk tls_psk_runtime
tls_psk_contract tls_acme_credentials tls_acme_selection tls_acme_lifetime
tls_io tls_io_race tls_io_loopback io_tls_control
```

Build those targets with `make -C build-on/test -j2`; run
`make -C build-on/test -j1 check-TESTS` with **both** `check_PROGRAMS` and `TESTS`
set to that list. A Python `subprocess.run([...], timeout=180)` wrapper bounded
the gate. Logs are local generated files in `build-on/{baseline,focused}.log`
and `build-on/test/tls_policy_*.log`; they are not committed artifacts.

`build-san` uses the TLS-on configuration with
`CXXFLAGS='-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined'`
and `LDFLAGS='-L/opt/homebrew/lib -fsanitize=address,undefined'`. It builds
`src/libhttpserver_v3core.la` and both new executables from instrumented sources;
it does not reuse the ordinary archive. Run the two-target focused gate with
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. Logs are in
`build-san/{build,sanitizer}.log` and its individual test logs.

## Unexecuted evidence

BSD, Windows/IOCP, Linux-only backends and other nonlocal execution belong to CI
and the v3 PR under AGENTS.md. No QUIC execution, deployment or public TCP-443
listener proof is claimed. TSan and leak detection were not run; macOS ASan ran
with leak detection disabled. The reused OpenSSL provider itself was not rebuilt
with sanitizers. No external Groundwork validation/finalization phase was run.
