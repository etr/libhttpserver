# Implementation Plan: TASK-145 Gate HTTP/2 conformance, fuzzing and independent clients

## Cloud resume checkpoint (2026-10-07)

The user paused TASK-145 during implementation recovery. Resume from the WIP
`task/TASK-145` branch with `v3` as the integration base. Read
`docs/task-145-http2-evidence.md` for the current partial evidence and remaining
checks. Absolute paths and installed-tool locations below describe the original
macOS workspace: resolve current primary/task roots and cloud tool paths before
running commands; do not create a nested worktree. Rebuild local dependencies
and evidence in the cloud. TASK-145 is In Progress, with no sealed validation.
Local tests gate merge; BSD, Windows and other nonlocal checks remain CI-owned.

## Context
- Mode: task
- Identifier: TASK-145
- Branch prefix: task
- Specs dir: /Users/etr/progs/libhttpserver/.worktrees/TASK-145/specs
- Tasks path: /Users/etr/progs/libhttpserver/.worktrees/TASK-145/specs/tasks/M10-tls-http2/TASK-145.md

## Plan
Implement TASK-145 in the existing isolated worktree, extending the current conformance and fuzz infrastructure. External clients must exercise the real private HTTP/2 engine through a bounded test-only TLS fixture. This establishes engine interoperability; the public listener currently dispatches HTTP/1.

1. **Resolve identity and record the baseline.**

   Run:
   ```sh
   (cd /Users/etr/progs/libhttpserver && \
     GROUNDWORK_PROJECT_ROOT=/Users/etr/progs/libhttpserver \
     node /Users/etr/.codex/skills/groundwork-plan-task/scripts/worktree-identity.js TASK-145)
   ```
   The installed helper resolves `repo_root` from its working directory. Run this read-only identity check from the primary repository, overriding the runner's task-local `GROUNDWORK_PROJECT_ROOT` only for this subprocess. All implementation, builds and tests run in `/Users/etr/progs/libhttpserver/.worktrees/TASK-145`.
   Use the returned path and branch verbatim; require `base_branch=v3`. If the helper contradicts the registered existing worktree, return the exact identity error to the caller; do not derive another path or create a nested worktree. Read back branch, HEAD, dirty state and applicable `AGENTS.md`. Preserve unrelated work.

   Reuse compatible task-local build directories and the existing OpenSSL provider after checking its version and configuration. Dependencies TASK-138–144 are complete, but their historical receipts do not count as TASK-145 evidence. Implementation uses meaningful RED → GREEN → refactor cycles. Validation, status completion, commit, merge and cleanup remain caller-owned.

2. **Extend RFC 9113 transcripts through the request engine.**

   Current `test/unit/http2_conformance_test.cpp::replay` only drives `http2_connection`. Preserve its existing 28 framing/control fixtures and exact output checks.

   Add `test/unit/http2_engine_conformance_test.cpp`, a separate engine corpus under `test/conformance/http2-engine/`, and registration in `test/Makefile.am`. Reuse `protocol_corpus::load`, `http2_fixture.hpp` and `http2_request_fixture.hpp`; do not change the shared five-column corpus format.

   Use compact typed observations:
   ```cpp
   struct observation {
       size_t consumed;
       optional<http2_error> terminal;
       vector<wire_frame> output;
       vector<route_event> routes;
       budget_snapshot retained;
   };
   observation replay(entry, fragmentation, partial_output);
   ```
   Fixed binary transcripts and authored verdicts must cover:

   - Invalid HPACK indices, truncated integers/literals, Huffman EOS/padding, table-size updates in forbidden positions, and required minimum/final table-size updates across SETTINGS changes.
   - Pseudo-header ordering/duplicates, uppercase or invalid fields, forbidden connection fields, TE restrictions, authority/Host disagreement, missing required fields, content-length mismatch and malformed trailers.
   - DATA/RST_STREAM on idle or closed streams, invalid stream identifiers, stream versus connection window overflow, padded DATA accounting, continuation violations and EOF during a field block.
   - A rejected/reset stream followed by a sibling using the resulting dynamic table, proving discarded HEADERS still update compression state.
   - Extended CONNECT capability gating, malformed opening headers and isolation of an invalid WebSocket stream from an ordinary sibling.
   - RFC 9113 treatment of deprecated priority information, including self-dependency, while retaining mandatory frame-length, stream-ID and continuation rules.

   Replay coalesced, bytewise and at every split for bounded fixtures; advance output both fully and in small fragments. Assert observable error scope/code, affected stream, response/reset/GOAWAY ordering, sibling route effects, sticky connection failure and zero reservations after destruction. Keep expected HPACK octets independent of the library encoder.

   Add regression tests before any necessary repair. Limit repairs to the implicated existing symbols: `http2_frame_parser::{priority_rules,finish_frame}`, `http2_request_engine::{feed,eof}`, its `state::{peer_reset,fail,reset}`, request-head conversion, stream-flow handling or the existing HPACK decoder. Do not weaken verdicts to fit current behavior. RFC 9113 remains the oracle; its deprecated priority semantics and retained framing rules require separate checks. [RFC 9113](https://www.rfc-editor.org/rfc/rfc9113.html#section-6.3)

3. **Make HTTP/2 and HPACK fuzzing reproducible gates.**

   Reuse `test/fuzz/hpack_fuzz.cpp::{hpack_fuzz_input,hpack_fuzz_sections}` and `test/unit/hpack_corpus_test.cpp`; they already exercise bounded primitives, two-block compression state and deterministic mutations.

   Add committed malformed HPACK seeds under `test/data/hpack/seeds/`, with the eight control bytes required by the existing fuzz input format. Include RFC example blocks and the malformed families above. Add a deterministic seed replay case to `hpack_corpus`.

   Add `test/fuzz/http2_engine_fuzz.{hpp,cpp}` and `test/unit/http2_fuzz_replay_test.cpp`. Drive the actual `http2_request_engine` with fixed small stream/body/output budgets, a serialized executor, bounded routes and a fixed clock. Cap input at 64 KiB, owner-pump iterations and generated response bytes. Check consumption bounds, stable terminal behavior, output advancement, accounting limits and complete release on destruction. Compare equivalent input fragmentations under the same deterministic pumping schedule; avoid assertions whose result legitimately depends on route scheduling.

   Extend `scripts/run-v3-protocol-fuzz.sh` with explicit target selection so TASK-145 can request `hpack http2_engine` without rerunning unrelated fuzzers. Preserve current defaults. Use `HPACK_LIBFUZZER` for the existing HPACK entry and its 512-byte limit. Copy seeds into writable build-local corpora, retain crash artifacts and reproduction commands, and fail on missing instrumentation. Extend `test/integ/protocol_fuzz_runner_test.py` for selected-target validation and unavailable-tool behavior. Register all new corpus assets in `EXTRA_DIST`.

4. **Add one bounded TLS fixture and two independent client adapters.**

   Add `test/integ/http2_tls_fixture.cpp`, conditionally built under `NATIVE_V3_TLS`. Compose:

   ```text
   loopback socket, port 0
       → existing tls_session, credentials registry, negotiated ALPN h2
       → existing http2_request_engine
       → existing route_registry and manual_executor
   ```
   Reuse credential loading patterns from `tls_credentials_fixture.hpp` and TLS composition patterns from `http2_tls_boundary_test.cpp`. Pump `tls_session::{feed,drain,handshake,read,write}` and engine `feed/output/advance_output` with bounded buffers, deadlines and partial-write handling. Serialize all engine/route operations. Bind only loopback; publish `READY <port>` after listen. Bound accepted connections, active streams, total test bytes and lifetime. On STOP, EOF, timeout or client failure, cancel operations, destroy the engine and close sockets.

   Fixture routes should be only `/` for h2spec GET/POST returning 200 with nonempty data, `/hold`, `/health`, `/upload-hold`, `/large` and `/ws`. Use stdin commands to release suspended routes/body reads and inspect bounded event counters; no readiness sleeps. Record enough passive DATA/window and handler-completion evidence to prove flow stalls and cancellation without adding production observability APIs.

   Add:
   - `test/integ/http2_client_h2.py`: pinned hyper-h2 **4.4.1**, Python TLS with ALPN `h2`.
   - `test/integ/http2_client_node.mjs`: Node **24.15.0**, `node:http2`, recording `process.versions.nghttp2`.

   These are separate HTTP/2/HPACK implementations. Both document Extended CONNECT support. Keep client dependency installation in a task-local environment and add a small pinned test requirements file; no production dependency changes. [hyper-h2 documentation](https://python-hyper.org/projects/hyper-h2/en/stable/), [Node 24.15 HTTP/2 documentation](https://nodejs.org/download/release/v24.15.0/docs/api/http2.html#the-extended-connect-protocol)

   Recovery setup: hyper-h2 4.4.1 requires Python >=3.10. The system Python 3.9.6 environment at `/private/tmp/task145-client-env` cannot install this pin; that failure does not establish release unavailability. Reuse the task-local Python 3.12.13 environment at `build/task145-client-env` and `test/integ/http2-client-requirements.txt`:
   ```sh
   python3.12 -m venv build/task145-client-env # only if absent
   build/task145-client-env/bin/python -m pip install -r test/integ/http2-client-requirements.txt
   build/task145-client-env/bin/python -m pip check
   ```
   Pass this environment's absolute Python path as `<pinned-python>` to the gates. Preserve the hyper-h2 4.4.1 pin.

   Each client must independently pass the same scenarios on one connection:

   - `/hold` remains suspended while `/health` completes; release then completes the held stream.
   - Client CANCEL resets a suspended/uploading stream; cancellation occurs once and a sibling completes.
   - Uploads exceed both stream and aggregate connection credit with fixture body consumption held. Observe bounded DATA/stall, then release reads and verify resumed transfer with exact byte counts.
   - Small response stream window stalls `/large`; increasing credit resumes complete, exact delivery.
   - Await server `SETTINGS_ENABLE_CONNECT_PROTOCOL=1`, send valid RFC 8441 headers without END_STREAM, exchange masked text/binary WebSocket messages and ping/pong, complete Close, and complete a sibling GET while the WebSocket is active.

   A small client-side WebSocket framing helper is acceptable; HTTP/2 and HPACK must come from each independent stack. HTTP/1 WebSocket receipts, raw transcripts and OpenSSL-only peers cannot substitute for either client. If a pinned client cannot perform a required scenario, report its exact capability/dependency blocker.

5. **Add supplemental h2spec evidence with checked exclusions.**

   Add a TASK-145 runner, preferably `scripts/run-v3-http2-gates.py`, plus `test/integ/http2_gate_runner_test.py`. Give it explicit build, log, Python, Node, h2spec and compiler arguments. Required requests fail on absent dependencies, fixture startup/ALPN failure, timeout, subprocess failure, empty coverage or malformed result output. Preserve logs and subprocess statuses; always reap fixture processes.

   Pin h2spec **v2.6.0** and record binary/source identity. It targets RFC 7540/7541, so its results supplement the RFC 9113 corpus. [h2spec usage and versions](https://github.com/summerwind/h2spec)

   Run TLS mode with explicit host/port, bounded per-case timeout and machine-readable results. Capture the full `--dryrun` inventory first. Add `test/conformance/http2/h2spec-exclusions.tsv` containing exact discovered case IDs, legacy assertion, RFC 9113 section and rationale. Map exclusions from the pinned tool’s actual inventory/source; do not invent IDs.

   Only demonstrated obsolete assertions, such as deprecated priority semantics or h2c Upgrade requirements if present, qualify. Continue running retained PRIORITY framing tests. Do not exclude entire sections, compression errors, flow failures, malformed-header failures, timeouts or fixture bugs. Require every discovered case to be either executed or explicitly excluded; unexpected failures remain fatal. A genuinely empty exclusion list is valid if no incompatible assertions occur. [RFC 9113 changes from RFC 7540](https://www.rfc-editor.org/rfc/rfc9113.html#appendix-B)

6. **Run bounded local implementation checks and record evidence.**

   Following bootstrap/configuration, rebuild the native library before linked tests. Use C++20 TLS-off and TLS-on builds; TLS-off covers deterministic engine/HPACK suites, TLS-on adds actual external TLS clients and h2spec.

   Focused selection:
   ```text
   http2_frame http2_settings http2_connection http2_conformance
   http2_engine_conformance http2_fuzz_replay
   http2_headers http2_exchange http2_streaming http2_flow_control
   http2_fair_output http2_reset http2_rate_budget http2_drain
   http2_websocket_handshake http2_websocket http2_websocket_flow
   http2_websocket_lifecycle hpack_primitives hpack_corpus
   hpack_dynamic_table hpack_field_section hpack_connection
   ```
   TLS-on adds `http2_tls_boundary` and builds `http2_tls_fixture`.

   Commands, with the selected build and tools substituted:
   ```sh
   make -C <build>/src -j2
   make -C <build>/test -j2 <selection>
   make -C <build>/test -j1 check-TESTS \
     check_PROGRAMS='<selection>' TESTS='<selection>'
   make -C <build> check-local
   python3 test/integ/http2_gate_runner_test.py
   python3 test/integ/protocol_fuzz_runner_test.py
   python3 scripts/run-v3-http2-gates.py --build-dir <tls-on-build> \
     --log-dir <task-log-dir> --python <pinned-python> \
     --node <pinned-node> --h2spec <pinned-h2spec>
   scripts/run-v3-protocol-fuzz.sh --build-dir <task-fuzz-dir> \
     --native-config-dir <tls-off-build> \
     --compiler /opt/homebrew/opt/llvm/bin/clang++ \
     --targets 'hpack http2_engine' --runs 2000 --seconds 30
   ```
   Run the new deterministic conformance/replay suites under ASan/UBSan with the library instrumented. Run changed-file cpplint, changed-source complexity, `scripts/check-file-size.sh` and `git diff --check`. Repository-wide formal validation is separate.

   Write `docs/task-145-http2-evidence.md` with revision, build/provider/tool versions, commands, actual counts, client scenario receipts, h2spec inventory/exclusions/results, fuzz bounds and sanitizer limitations. Acceptance requires both real client matrices, RFC 9113 malformed transcripts, deterministic malformed seeds, bounded fuzz smoke and supplemental h2spec to pass without unexplained failures. BSD, Windows and other nonlocal checks remain CI/v3-PR owned and are recorded as unexecuted.

   Current dependency observations: Node 24.15.0 and Homebrew LLVM are present; h2spec and Go were not found on PATH. The implementer must obtain a pinned compatible h2spec binary or build its pinned source in task-local tooling. Missing h2spec remains missing local evidence, not an RFC exclusion.
