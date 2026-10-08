### TASK-150: Add deterministic QUIC network, clock and fuzz harness

**Milestone:** M11 - First HTTP/3 slice
**Component:** QUIC v1 engine
**Estimate:** M
**Branch policy:** Create this task branch/worktree from `v3` and merge the validated task into `v3`; require `base_branch=v3`.

**Goal:**
Provide deterministic QUIC network, clock and fuzz harness for libhttpserver v3.0.

**Action Items:**
- [x] Build simulated clock and datagram loss/reorder/duplication network.
- [x] Assert timers, emitted packets and retained bytes.
- [x] Seed QUIC parser and state-machine fuzz harnesses.

**Dependencies:**
- Blocked by: TASK-149
- Blocks: TASK-151

**Acceptance Criteria:**
- Scripted loss, reorder, duplication and timer traces replay byte-for-byte with resource accounting.
- C++20 build and focused tests pass.

**Related Requirements:** PRD-V3N-REQ-007, PRD-V3N-REQ-008
**Related Decisions:** DR-V3-001, DR-V3-006

**Status:** In Progress


**Implementation evidence (2026-10-08, local macOS):**
- Added bounded test support for a logical nanosecond clock, whole owned datagrams, explicit loss/delivery selection/duplication, four real CID routes and owner executors, and real fake-backend timer lifecycle. Independent copies consume network capacity; owner admission charges and packet reclamation are verified separately.
- Canonical versioned traces encode explicit integer fields, datagram bytes/metadata, timer results and resource snapshots. A fixed expected teardown trace and independently asserted packet/timer/accounting outcomes supplement byte-for-byte fresh-rig replay. Timer teardown completes in submission order rather than depending on backend unordered-map iteration.
- Added invariant-header parser and dispatch/ownership/timer state fuzz targets, committed binary seeds, seed/action/trace documentation, ordinary fixed-seed mutation replay, and explicit runner targets. Coverage does not include a QUIC transport engine, packet protection, loss recovery, streams, QPACK or HTTP/3 request serving.
- TDD: clock/network tests failed against empty support implementations; parser/state replay tests failed against empty target implementations; runner tests failed before QUIC target recognition/configuration checks. All subsequently pass.
- `./bootstrap`; configured `build/task150` with `--disable-v3-tls --disable-examples CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib`; full `make -C build/task150/src -j2` passes with mandatory C++20.
- Focused `check-TESTS`: six targets pass (`quic_network_harness`, `quic_fuzz_replay`, `quic_invariant_header`, `quic_datagram_dispatch`, `fake_io_backend`, `io_connection_owner`). The existing dispatcher loopback test needs local UDP access; its sandbox timeout passes with permitted access. The two new targets also pass in the sandbox after the final seed/test updates.
- `python3 test/integ/protocol_fuzz_runner_test.py`: five tests pass, including missing compiler and missing/TLS-on configuration rejection. `bash -n scripts/run-v3-protocol-fuzz.sh` passes.
- Instrumented libFuzzer/ASan/UBSan with `/opt/homebrew/opt/llvm/bin/clang++`, `--native-config-dir build/task150 --targets 'quic_parser quic_state' --runs 2000 --seconds 30`: both targets pass 2000 runs. Native state seams are compiled with instrumentation; source seeds stay immutable. All committed seeds, including the final loss/reorder and truncated-source additions, pass ordinary replay/mutation tests.
- Changed C++ files pass repository cpplint; `git diff --check` passes. BSD, Windows and other nonlocal platform execution remains assigned to CI/v3 PR by `AGENTS.md`.
- Status remains In Progress pending caller-owned validation/finalization; implementation edits are unstaged for the runner.
