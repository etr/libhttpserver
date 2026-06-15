### TASK-081: Fill empty-on-correct-build unit suites and re-enable pthread leak detector

**Milestone:** M7 - v2 Cleanup
**Component:** `test/unit/`
**Estimate:** M

**Goal:**
Several unit suites have near-zero effective coverage on the CI lanes that matter:
- `test/unit/webserver_ws_unavailable_test.cpp:30-33` — entire suite empty on `HAVE_WEBSOCKET`-on builds.
- `test/unit/webserver_dauth_unavailable_test.cpp:33-37` — entire suite empty on `HAVE_DAUTH`-on builds.
- `test/unit/header_hygiene_test.cpp:67-118` — pthread leak detector disabled on both libc++ and libstdc++ (every CI lane).
- `test/unit/webserver_register_ws_smartptr_test.cpp` — runtime half `#ifdef HAVE_WEBSOCKET`.
- `test/unit/http_request_operator_stream_test.cpp:57-140` — credential-redaction tests entirely behind `#ifdef HAVE_BAUTH`.
- `test/unit/body_test.cpp:198-291`, `http_response_factories_test.cpp:235-267`, `iovec_entry_test.cpp:85-100` — `#ifndef _WIN32` gates skip pipe/iovec body tests on Windows.

Make sure every suite actually exercises code on the build configuration where the feature is *available* (the inverse of what each gate currently does).

**Action Items:**
- [x] `webserver_ws_unavailable_test.cpp`: invert the guard so the suite exercises the `HAVE_WEBSOCKET`-off path (or rename + add a paired `webserver_ws_available_test.cpp` for the on-path). Goal: each build flag has a unit suite that runs.
- [x] `webserver_dauth_unavailable_test.cpp`: same pattern.
- [x] `header_hygiene_test.cpp`: investigate why the pthread leak detector was disabled on both stdlibs. Either fix the detector so it works (preferred — this is the entire purpose of the test) or delete the test with a documented `RELEASE_NOTES.md` entry. Coordinated with TASK-007.
- [x] `http_request_operator_stream_test.cpp`: split the credential-redaction tests into two — the `HAVE_BAUTH`-on variant pins redaction-when-set; the `HAVE_BAUTH`-off variant pins that the auth-related fields are absent from the stream.
- [x] `body_test.cpp` / `http_response_factories_test.cpp` / `iovec_entry_test.cpp`: write a Windows-shaped variant of the pipe/iovec tests using Windows native equivalents (CreateFileMapping for the fd source, etc.) or document the gap in `test/PORTABILITY.md`.
- [x] `webserver_register_ws_smartptr_test.cpp`: same pattern as `webserver_ws_unavailable_test`.

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- Every unit suite exercises non-trivial code on at least one CI lane.
- The pthread leak detector either works on libc++ + libstdc++ or is deleted.
- Windows lane runs the body/response/iovec variants (or PORTABILITY.md records the gap).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-FLG-REQ-003 (`features()` coverage)
**Related Decisions:** None new

**Status:** Done
