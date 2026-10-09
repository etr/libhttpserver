# TASK-176: bounded QUIC recovery and TLS callback sequences

Local implementation evidence on 2026-10-09, Linux x86_64/WSL2. Base revision:
`b8e876ad2fa051ce2934922b46e343da66e83939`, branch `task/TASK-176` from `v3`.
The results below include the uncommitted TASK-176 implementation and its local
repair. Lifecycle completion, formal review and commits remain runner-owned.

## Implementation and reproducibility

The new recovery target drives the actual provider-free recovery, reassembly,
flow-control, identity and lifecycle internals. The callback target drives the
private OpenSSL dispatch table, bounded callback storage, key installation and
recovery/lifecycle state. Corpus formats and seed coverage are documented in
`test/data/quic/recovery/README.md` and `test/data/quic/callbacks/README.md`.

Both targets cap input at 4096 bytes, events at 64, and generated payloads at
16 bytes. Every input runs twice against fresh state; normalized observations
must match. Storage, receive ranges, sent packets, information records, leases
and datagram batches have independent ceilings. Final reservations must be zero.
Logical timer increments are bounded and monotonic. Close interleavings are
owner-serialized. Queued events never call through freed callback arguments.

Compiler: `/usr/bin/clang++`, Ubuntu Clang 18.1.3. Native provider:
`/home/etr/progs/libhttpserver/build/tooling/provider`, OpenSSL 3.5.9.
TLS-off configuration uses `--disable-v3-tls --disable-examples`, and TLS-on uses
`--enable-v3-tls --disable-examples`; both use `CC=clang CXX=clang++` and
`CXXFLAGS='-std=c++20 -O1 -g'`. TLS-on explicitly sets:

```sh
V3_TLS_CFLAGS=-I/home/etr/progs/libhttpserver/build/tooling/provider/include
V3_TLS_LIBS='-L/home/etr/progs/libhttpserver/build/tooling/provider/lib -lssl -lcrypto'
```

The fuzzer compiles project sources directly with
`-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined`, C++20,
`-O1 -g -fno-omit-frame-pointer`. It includes v3core sources for recovery and
v3core plus v3tls sources for callbacks, rather than ordinary project archives.
Provider flags come from literal configured Makefile substitutions parsed into
argument arrays without shell evaluation. Wrong TLS mode, missing or malformed
provider flags, missing instrumentation and empty corpora fail explicitly.
Existing default targets are unchanged.

Initial fuzz build/run commands from the worktree root:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  scripts/run-v3-protocol-fuzz.sh --compiler /usr/bin/clang++ \
  --build-dir build/task176-fuzz-off --native-config-dir build/task176-off \
  --targets quic_recovery --runs 2000 --seconds 30
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  scripts/run-v3-protocol-fuzz.sh --compiler /usr/bin/clang++ \
  --build-dir build/task176-fuzz-on --native-config-dir build/task176-on \
  --targets quic_tls_callbacks --runs 2000 --seconds 30
```

The local repair reran each generated `reproduce.sh` with those sanitizer
environment settings against the existing evolved build-local corpus. Generated
`compile.sh` records exact compiler arguments, `sources.list` records instrumented
project sources, `provider.log` records configured linkage, and `environment.log`
records compiler and revision. Each invocation uses seed 128, 2000 runs,
30-second total ceiling, 5-second per-input timeout, 512 MiB RSS ceiling, and a
separate artifact directory. Build-local corpora may grow; tracked authored seeds
remain unchanged.

## Local results

Prior-session baseline logs report 4/4 focused tests in each TLS configuration.
The repair reproduced three failed TLS owner assertions: a test expected an
incoming CRYPTO packet to execute while a submitted send was pinned. The owner
intentionally drops payload in that interval. The repaired test first asserts
active state and no close, completes the send, then redelivers the delayed packet.
Malformed post-handshake CRYPTO invokes the actual provider alert callback and
ordinary close arbitration in the network fixture. The test verifies one close,
cancellation of suspended handler work, rejection of later datagrams, application
key disposal and full storage release, both with and without a preceding send.

| Check | Result | Local log |
| --- | --- | --- |
| TLS-off focused C++20 suite | 17/17 pass | build/task176-off/focused-repair.log |
| TLS-on focused C++20 suite | 6/6 pass | build/task176-on/focused-repair.log |
| Recovery replay | 5 tests, 12 checks; 325 corpus/mutation/prefix cases | build/task176-off/test/quic_recovery_fuzz_replay.log |
| Callback replay and real owner handshake | 7 tests, 38 checks; 585 corpus/mutation/prefix cases | build/task176-on/test/quic_tls_callbacks_fuzz_replay.log |
| Fully instrumented ASan/UBSan replay suite | 2/2 pass, including real-handshake close interleavings | build/task176-san-on/replay-repair.log |
| Recovery ASan/UBSan fuzzer rerun | 2000 runs, cov 2056, ft 4880, peak reported RSS 251 MiB | build/task176-fuzz-off/quic_recovery/repair-run.log |
| Callback ASan/UBSan fuzzer rerun | 2000 runs, cov 951, ft 2078, peak reported RSS 403 MiB | build/task176-fuzz-on/quic_tls_callbacks/repair-run.log |
| Runner preflight tests | 7/7 pass | build/task176-off/runner-repair.log |
| Native linkage audits | Pass in TLS-off and TLS-on | build/task176-{off,on}/linkage-repair.log |
| Changed C++ files cpplint | Pass | build/task176-off/lint-repair.log |
| Source distribution staging | Pass | build/task176-off/dist-repair.log |

The TLS-off suite selects quic_fuzz_replay, quic_recovery_fuzz_replay,
quic_reassembly, quic_ack_state, quic_recovery, quic_repacketize,
quic_flow_control, quic_flow_recovery, quic_flow_storage, quic_congestion,
quic_pacing, quic_send_schedule, quic_connection_identity, quic_path_mtu,
quic_datagram_sizing, quic_connection_lifecycle and v3_native_linkage.
The TLS-on suite selects quic_recovery_fuzz_replay,
quic_tls_callbacks_fuzz_replay, quic_tls_session, quic_tls_handshake,
quic_recovery_tls and http3_network_owner. Reproduce each suite using:

```sh
make -C build/task176-off/test -j4 check TESTS="$off_tests" check_PROGRAMS="$off_tests"
make -C build/task176-on/test -j4 check TESTS="$on_tests" check_PROGRAMS="$on_tests"
make -C build/task176-off check-v3-native-linkage
make -C build/task176-on check-v3-native-linkage
python3 test/integ/protocol_fuzz_runner_test.py
make -C build/task176-off distdir
git diff --check
```

Set `off_tests` and `on_tests` to the corresponding space-separated selections
above. Running tests through their configured test directory resolves certificates.

Authored recovery seeds attempt all 24 operation kinds; 23 kinds have successful
operations (the explicit datagram-abandon seed exercises a rejected operation).
They acknowledge two packets despite reversed ACK order and an intervening loss
timer, deliver four reordered CRYPTO bytes, activate a validated path after a
pending send retires, and reject queued work after close. Callback seeds attempt
all 19 kinds, including all three encryption levels, held leases, backpressure,
AES-128/AES-256/ChaCha transitions, malformed parameters, invalid release,
dispatch alerts, missing-cipher failure, close and late events. Failure callbacks
return zero by contract; their accepted counters are not successful-error counts.
Per-kind attempted/accepted counts are printed in both replay logs. Scripted
callback coverage and the additional real-handshake owner test are distinct.

## Sanitizer replay and limitations

The separate `build/task176-san-on` configuration rebuilds project archives and
both replay executables with
`CXXFLAGS='-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined'`
and `LDFLAGS=-fsanitize=address,undefined`, using the same explicit provider.
Configure initially failed because LeakSanitizer cannot run under the local
ptrace sandbox, as recorded in its initial `config.log`/`configure-task176.log`.
The successful retry is in `configure-repair.log`; the instrumented replay suite
passes 2/2 in `replay-repair.log`. Leak detection is disabled
for this local lane while ASan and nonrecovering UBSan remain enabled. Resource
release is additionally checked through deterministic budget assertions.

```sh
ASAN_OPTIONS=detect_leaks=0 make -C build/task176-san-on/src -j4 \
  libhttpserver_v3core.la libhttpserver_v3tls.la
ASAN_OPTIONS=detect_leaks=0 make -C build/task176-san-on/test -j4 \
  quic_recovery_fuzz_replay quic_tls_callbacks_fuzz_replay
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  make -C build/task176-san-on/test check \
  TESTS='quic_recovery_fuzz_replay quic_tls_callbacks_fuzz_replay' \
  check_PROGRAMS='quic_recovery_fuzz_replay quic_tls_callbacks_fuzz_replay'
```

OpenSSL itself is reused from the existing provider build and is not claimed
to be instrumented. These are bounded local traces, not exhaustive fuzzing,
concurrent callback stress, UDP/client interoperability or complete HTTP/3 proof.
No sanitizer crash was found in the recorded runs, so no minimized crash seed
is claimed; committed seeds are authored coverage and deterministic regressions.
Future crashes must retain the original artifact and reproduction command and
add a minimized input with a named invariant test.

The library source size gate reports the unchanged `src/detail/io_poll_backend.cpp`
at 509 SLOC against a 500 ceiling. The complexity gate, using the existing
`/tmp/task166-lint-tools` lizard installation, reports 49 existing violations.
`git diff --quiet HEAD -- src` passes: all checked library source is identical
to the baseline. Thresholds are unchanged. Logs are
`build/task176-off/file-size-repair.log` and `complexity-repair.log`.
BSD, Windows and other nonlocal checks remain unexecuted locally and are owned
by CI/the v3 PR under AGENTS.md.
