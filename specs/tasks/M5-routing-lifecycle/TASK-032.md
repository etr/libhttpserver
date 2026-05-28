### TASK-032: Thread-safety contract stress test (DR-008)

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Concurrency
**Estimate:** M

**Goal:**
Verify the documented thread-safety contract: `webserver` public methods are reentrant from inside a handler, except `stop()` and `~webserver()` which deadlock by design.

**Action Items:**
- [x] Write a stress test (`test/integ/threadsafety_stress.cpp`) that runs 16 concurrent curl clients, each request randomly invoking `register_path` (the non-deprecated successor to `register_resource`), `unregister_path`, `block_ip`, `unblock_ip` against the running `webserver` from inside an on_get lambda handler. Default duration 60 s; override with `HTTPSERVER_STRESS_SECONDS=N`.
- [x] Run under ThreadSanitizer in CI; the existing `build-type: tsan` matrix entry in `.github/workflows/verify-build.yml` invokes `make check`, which auto-picks up every new `check_PROGRAMS` entry — no workflow edit needed.
- [x] Add a separate test (`stop_from_handler_deadlocks_as_documented`) that calls `stop()` from inside a handler thread; opt-in via `HTTPSERVER_RUN_STOP_FROM_HANDLER=1`. The test forks a child so the abort/deadlock does not crash the test binary; either a non-zero child exit (libmicrohttpd self-join abort) or a 5 s timeout counts as positive observation of the contract.
- [x] Document the deadlock case in `webserver::stop()` and `~webserver()` Doxygen.

**Dependencies:**
- Blocked by: TASK-027, TASK-031
- Blocks: TASK-041

**Acceptance Criteria:**
- TSan-clean run of the stress test for at least 60 seconds with concurrent register/lookup/block.
- The stop-from-handler test reproduces the documented deadlock (or completes within a deliberately long timeout that confirms the wait behavior).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 NFR (concurrency)
**Related Decisions:** DR-008, §5.1, §9 testing item 6, AR-006

**Status:** Done
