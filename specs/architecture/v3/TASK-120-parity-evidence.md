# TASK-120 implementation evidence

Base: `v3` at `811e171`; worktree `.worktrees/TASK-120`, branch
`task/TASK-120`. Commands ran from this worktree's `build/test` unless
specified otherwise. The parent owns review, validation and final status.

## Failing-before native evidence

Before the production marker/framer change, the real native replay failed:

```text
icy_status_line: expect-line=10: expected status line "ICY 200 OK", got "HTTP/1.1 200 OK"
```

The expanded native suite then reported 4 tests / 23 checks / 5 failures:
the sync ICY case, both shared-definition sends, HTTP/1.0 and HEAD all
received ordinary HTTP status lines. File nonempty/empty/missing cases
passed. Baseline `http_status` passed 6 tests / 81 checks.

## Exact v2 recording provenance and unresolved recorder defect

Command: `./transcript_runner --record file_resp`.
Actual exit: **1**, not a passing recording gate.
Fixture: `test/parity/v2_fixture.cpp` (`build_file_resp`, actual legacy
`http_response::file`, `pipe`, `iovec`, `deferred` factories, running on
`webserver` / MHD, not native v3). Fixture SHA-256:
`80d4467d0334247b860a6e99cae301492d748c2c6d8a7397754ae0d16a46e213`.

[Retained recorded output](../../../test/parity/transcripts/file_resp.tseq.recorded)
SHA-256: `f9dc830c71450f033bbeec7d27db787112692374e250a2925aa0bf36cef119d3`.
The sidecar contains observed body/fields/framing and an `ok` keepalive
probe outcome for every case. The committed `file_resp.tseq` preserves
those observations; Date is normalized. New mappings:

| Case | Observed v2 body | Observed framing | Native replay adapter |
|---|---|---|---|
| `pipe_body` | `abcXYZ` | chunked | `response_definition::owned_pipe` |
| `iovec_body` | `abcXYZ` | Content-Length 6 | per-send factory over two borrowed spans |
| `deferred_body` | `abcXYZ` | chunked | per-send factory with unknown length |

The existing record branches in `test/transcript_runner.cpp` write the
sidecar and `continue` before `++out.cases_run`. The suite's
`LT_CHECK(outcome.cases_run > 0)` therefore fails even after observed
responses are written. This accounting defect remains unchanged in
TASK-120; validation must assess the nonzero recording result separately
from the observations and normal replay. Exact final recorder output:

```text
Running test (1): run_corpus_routing
[SKIP] (../../test/transcript_runner.cpp:585) - skipping "run_corpus_routing": excluded by filter
- Time spent during "run_corpus_routing": 0.0820312 ms
Running test (2): run_corpus_hooks
[SKIP] (../../test/transcript_runner.cpp:586) - skipping "run_corpus_hooks": excluded by filter
- Time spent during "run_corpus_hooks": 0.00708008 ms
Running test (3): run_corpus_auth_basic
[SKIP] (../../test/transcript_runner.cpp:587) - skipping "run_corpus_auth_basic": excluded by filter
- Time spent during "run_corpus_auth_basic": 0.00488281 ms
Running test (4): run_corpus_auth_digest
[SKIP] (../../test/transcript_runner.cpp:588) - skipping "run_corpus_auth_digest": excluded by filter
- Time spent during "run_corpus_auth_digest": 0.00488281 ms
Running test (5): run_corpus_forms
[SKIP] (../../test/transcript_runner.cpp:589) - skipping "run_corpus_forms": excluded by filter
- Time spent during "run_corpus_forms": 0.0400391 ms
Running test (6): run_corpus_file_resp
[CHECK FAILURE] (../../test/transcript_runner.cpp:590) - error in "run_corpus_file_resp"
- Time spent during "run_corpus_file_resp": 5.27808 ms
Running test (7): run_corpus_ip_controls
[SKIP] (../../test/transcript_runner.cpp:591) - skipping "run_corpus_ip_controls": excluded by filter
- Time spent during "run_corpus_ip_controls": 0.00610352 ms
Running test (8): run_corpus_shoutcast
[SKIP] (../../test/transcript_runner.cpp:592) - skipping "run_corpus_shoutcast": excluded by filter
- Time spent during "run_corpus_shoutcast": 0.00610352 ms
Running test (9): run_corpus_websocket
[SKIP] (../../test/transcript_runner.cpp:593) - skipping "run_corpus_websocket": excluded by filter
- Time spent during "run_corpus_websocket": 0.00488281 ms
Running test (10): run_corpus_tls
[SKIP] (../../test/transcript_runner.cpp:594) - skipping "run_corpus_tls": excluded by filter
- Time spent during "run_corpus_tls": 0.00488281 ms
** Runner terminated! **
10 tests executed
1 checks
-> 0 successes
-> 1 failures
-> 9 skipped
Total run time: 5.43896 ms
Total time spent in tests: 5.43896 ms
Average set up time: 0 ms
Average tear down time: 0 ms
```

## Passing gates (separate from recording)

- C++20 VPATH build: `CXX='clang++ -std=c++20'`,
  `CPPFLAGS=-I/opt/homebrew/include`,
  `LDFLAGS='-L/opt/homebrew/lib -lgnutls'`; Doxygen enabled.
- All 25 planned focused executables exited 0: native parity,
  status/mode/framer/outbox, connection engine, lifecycle, sync route,
  response definitions/sources, four affected consumers, header hygiene,
  native linkage, native end-to-end, Basic/Digest/forms/routing/hooks/IP
  corpus targets and full v2 transcript runner.
- Full `make check -j1` from `build`: exit 0, **205/205 PASS**, zero
  skip/fail/error. Top-level example/consumer, lint, staged-install,
  header-hygiene and Doxygen checks passed (zero substantive warnings).
- CI `python3 -m cpplint --extensions=cpp,hpp --headers=hpp` on all 11
  touched C++ files: exit 0; repository lint targets and `git diff --check`
  passed. Small include/brace cleanups were confined to the touched fixture.
- After those lint-only cleanups, rebuilt `native_http1_parity`,
  `transcript_runner`, `v2_profile_registry`; all three normal executions
  exited 0. Final native replay: **6 tests / 30 checks, zero failures**.
  Final full normal `./transcript_runner`: exit 0; recording exit 1 remains
  distinct as documented above.

The coverage inventory maps remaining HTTP/1 behavior to meaningful
executable cases or named migration exceptions. TLS/WebSocket native
coverage is deferred to its owning tasks, not claimed here.
