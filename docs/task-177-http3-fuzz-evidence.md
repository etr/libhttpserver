# TASK-177 HTTP/3 and QPACK fuzz evidence

## Scope and ownership

Implementation resumes the registered `.worktrees/TASK-177` worktree on
`task/TASK-177`, based on `v3` commit `9ffaa42d`. The runner owns staging,
commits, validation and finalization. Task tracking remains **In Progress**
until those phases finish. No production sources or public interfaces changed.

The two private test harnesses exercise HTTP/3 framing/control streams, QPACK
primitives/instructions/field sections, request/encoder ordering, blocked
cancellation and decoded limits. A composed request-engine replay covers
blocked HEADERS with FIN, cancellation, repeated field values, bounded response
output and reservation teardown. This supports PRD-V3N-REQ-007/008/017/027 and
DR-V3-001 without moving compression state into semantic exchanges.

Corpus formats, operations, profiles and ceilings are documented in
`test/data/http3/README.md` and `test/data/qpack/seeds/README.md`. All authored
seeds and both READMEs are registered in `test/Makefile.am`; Automake registers
the replay executables through the existing `TESTS = $(check_PROGRAMS)` rule.
Explicit fuzz selection adds `http3_engine` and `qpack`; defaults remain the
three existing HTTP/1/WebSocket targets.

## RED/GREEN receipts

The resumed implementation includes these earlier task-local RED receipts:

- `/tmp/task177-runner-red.log`: six failing selected-target subcases because
  `http3_engine` and `qpack` were unrecognized (exit 2 rather than the expected
  compiler/configuration failure). The implemented runner now passes all ten
  Python runner tests, including sanitizer flags, source instrumentation,
  corpus copying and bounded invocation.
- `/tmp/task177-qpack-red.log` and `/tmp/task177-http3-red.log`: replay tests
  initially could not compile because the harness headers did not exist.
  The resumed GREEN run passes both replay suites, including explicit field,
  error, admission, cancellation, unblock and decoded-limit assertions.
- The first sanitizer HTTP/3 run stopped at `reset releases blocked reservation`
  (`build/task177-fuzz/http3_engine/run.log`). Its transcript delivered EOF to
  a blocked section, then repeated RESET. Production preserves the first
  terminal notification. The harness now records terminal delivery and only
  treats the first RESET as cancellation. The minimized deterministic test
  `delivered_fin_is_not_replaced_by_repeated_terminal` requires later insertion
  to deliver exactly one HEADERS event and release the blocked reservation.
  This was a harness assumption correction; no production fix was needed.

Only the saved receipts above establish earlier RED observations; the resumed
executor did not recreate already completed implementation steps.

## Resource and operation bounds

| Harness | Bound |
|---|---|
| HTTP/3 input/events/streams | 4096 bytes / 64 events / 16 stream records |
| HTTP/3 feed calls | 4096, with production yield serviced by a defined turn |
| HTTP/3 copied observations and output | 16 KiB total / 2048 observations |
| HTTP/3 storage pool | 16 KiB data / 64 KiB critical, charged to an ancestor |
| HTTP/3 headers | 128 compressed bytes / 256 expanded bytes / eight fields |
| HTTP/3 QPACK | 128-byte table per direction / two blocked sections |
| HTTP/3 retained HEADERS | 256 bytes, including held-event boundary profiles |
| HTTP/3 instruction/output | 128 bytes per instruction/output direction |
| HTTP/3 outgoing/SETTINGS | Eight sections / 128 bytes and eight identifiers |
| HTTP/3 DATA/turn | 16-byte chunks / two frames or instructions per turn |
| QPACK input/instructions | 512 bytes / at most 32 per direction |
| QPACK table/decoded fields | 128 bytes per direction / 256 bytes and eight fields |
| QPACK primitive/instruction/trace | 128 bytes / 128 bytes / 2048 trace entries |

Boundary profiles include zero limits, the exact 42-byte expanded static GET
limit, one-below admission limits, blocked-count and retained-byte saturation,
and a 127-byte child table budget below the required 128-byte reservation.

Checks cover consumed-byte bounds, exact stream offset advancement, resource
capacity after each event/instruction, stable connection errors, no resurrection
of cancelled sections, output refusal preserving pending bytes, ordered
round-trips with never-indexed flags, and zero outstanding reservations after
connections/tables/pools leave scope. Pool envelopes remain legitimately charged
while leases exist. Work ceilings combine finite inputs/tables/streams, production
turn yields and a hard harness call cap; they do not prove a wall-time bound for
every internal instruction or provide a global allocation census.

Known regressions require exact H3 wire codes `0x103`, `0x104`, `0x105`,
`0x106`, `0x107`, `0x109`, `0x10a`, `0x200`, `0x201` and `0x202`.
An invalid feed offset checks the existing `0x102` internal error. The harness
checks connection scope and existing outcome mapping: `0x107` maps to
`limit_exceeded`; the other admitted failures map to `protocol_error`.
QPACK failures check the existing typed states and transactional codec contract;
`incomplete` and `inserts_unavailable` are admitted intermediate outcomes.

Every HTTP/3 input runs twice on fresh state and once with one-byte fragments.
Valid results compare normalized observations/output and counters; malformed
inputs compare the wire error. QPACK inputs compare two fresh executions.
Sorted corpus replay uses mutation PRNG seed 176, 32 mutations and 32 truncated
prefixes per seed. LibFuzzer uses seed 128 and writable corpus copies.

## Local commands and results

Configuration is Linux x86_64 with Ubuntu Clang 18.1.3, Autoconf/Automake and
C++20. Saved setup receipts show:

```sh
./bootstrap
mkdir -p build/task177-off
cd build/task177-off
../../configure CC=clang CXX=clang++ CXXFLAGS='-std=c++20 -O1 -g' \
  --disable-examples --disable-v3-tls
```

The pre-task focused baseline passed all 14 protocol suites
(`/tmp/task177-baseline-check.log`). Resumed local verification ran:

```sh
make -C build/task177-off -j6
make -C build/task177-off/test -j6 http3_fuzz_replay qpack_fuzz_replay
make -C build/task177-off/test check \
  TESTS='http3_fuzz_replay qpack_fuzz_replay http3_frame http3_settings http3_connection http3_qpack qpack_accounting http3_qpack_scheduling http3_drain qpack_primitives qpack_field_section qpack_dynamic_table qpack_instructions qpack_dynamic_corpus http3_streaming http3_cancellation' \
  check_PROGRAMS='http3_fuzz_replay qpack_fuzz_replay http3_frame http3_settings http3_connection http3_qpack qpack_accounting http3_qpack_scheduling http3_drain qpack_primitives qpack_field_section qpack_dynamic_table qpack_instructions qpack_dynamic_corpus http3_streaming http3_cancellation'
python3 test/integ/protocol_fuzz_runner_test.py
python3 -m cpplint --extensions=cpp,hpp --headers=hpp \
  test/fuzz/http3_engine_fuzz.cpp test/fuzz/http3_engine_fuzz.hpp \
  test/fuzz/qpack_fuzz.cpp test/fuzz/qpack_fuzz.hpp \
  test/unit/http3_fuzz_replay_test.cpp test/unit/qpack_fuzz_replay_test.cpp
bash -n scripts/run-v3-protocol-fuzz.sh
git diff --check
```

Project build passed. Focused tests passed **16/16**, with zero skips/errors.
HTTP/3 replay passed eight tests/119 checks and 2860 corpus/mutation/prefix
replays across 44 seeds. Authored counters include 19 decoded HEADERS, ten
blocked admissions, one cancellation, four unblocks and ten limit rejections.
QPACK replay passed four tests/24 checks and 1300 replays across 20 seeds;
six authored field sections decoded and round-tripped. Python runner tests
passed **10/10**. Changed-file cpplint, shell syntax and diff hygiene passed.
Receipts are in `/tmp/task177-resumed-{project-build,focused,runner,lint}.log`.
The retained-byte boundary assertions and their authored seeds now use blocked
sections: one three-byte section exactly fills the profile-5 aggregate budget;
a second blocked section fails with `0x107`. This avoids counting auto-released
static HEADERS as aggregate retained-state saturation. The added checks pass in
`/tmp/task177-resumed-retained.log`.

The repository file-size gate reports unchanged
`src/detail/io_poll_backend.cpp` at 509 SLOC (ceiling 500). TASK-177 changes no
`src/` paths; the baseline file is byte-identical to HEAD. No unrelated source
was edited to repair this existing repository-wide finding. Automake regeneration
also retains the pre-existing target-name warning at `test/Makefile.am:653`.

The required sanitizer invocation is:

```sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  scripts/run-v3-protocol-fuzz.sh --compiler /usr/bin/clang++ \
  --build-dir build/task177-resumed-fuzz \
  --native-config-dir build/task177-off --targets 'http3_engine qpack' \
  --runs 2000 --seconds 30
```

The script compiles the configured private native core sources with
`-fsanitize=fuzzer,address,undefined`, rather than linking an ordinary archive.
Each target records `sources.list`, `compile.sh`, `reproduce.sh`, `build.log`
and `run.log`, with timeout five seconds and RSS ceiling 512 MiB.
Both selected lanes passed **2000 runs** with no ASan/UBSan or invariant
failures and no compiler diagnostics. HTTP/3 finished its initial run in one
second at 87 MiB RSS; QPACK finished in under one reported second at 52 MiB RSS.
After refining the two aggregate-retention seeds, the existing instrumented
HTTP/3 binary passed another 2000 runs in one second at 84 MiB RSS:

```sh
cp test/data/http3/*.seed build/task177-resumed-fuzz/http3_engine/seeds/
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  bash build/task177-resumed-fuzz/http3_engine/reproduce.sh \
  > build/task177-resumed-fuzz/http3_engine/run.log 2>&1
```

The earlier passing HTTP/3 receipt is preserved as `run-initial.log`. Final
libFuzzer coverage counters were 1844 edges/4873 features for HTTP/3 and
819 edges/1953 features for QPACK; these are instrumentation counters, not
requirement-coverage percentages. Source corpus fingerprints (SHA-256 over
sorted filename, NUL, then seed contents) are:

- HTTP/3: 44 seeds, 811 bytes,
  `bf259abe932949931c8353ee7afc1468ba3b8e8616acba3e53ecb4cc19537231`.
- QPACK: 20 seeds, 198 bytes,
  `7d85a62c059abcb7e4056fdafc94398eea17f20850a6c2fbca63fa41cda66d3f`.

LeakSanitizer is disabled for the documented local ptrace limitation; explicit
reservation teardown checks still run. BSD, Windows, macOS and other nonlocal
checks were not executed and belong to CI and the v3 PR under the project
workflow policy. Authored regression coverage and bounded fuzz exploration do
not constitute exhaustive conformance proof.
