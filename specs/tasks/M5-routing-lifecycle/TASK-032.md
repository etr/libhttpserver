### TASK-032: Thread-safety contract stress test (DR-008)

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Concurrency
**Estimate:** M

**Goal:**
Verify the documented thread-safety contract: `webserver` public methods are reentrant from inside a handler, except `stop()` and `~webserver()` which deadlock by design.

**Action Items:**
- [ ] Write a stress test (`test/threadsafety_stress.cpp`) that runs N concurrent handlers, each randomly invoking `register_resource`, `block_ip`, `unblock_ip`, `unregister_resource` against the running `webserver`.
- [ ] Run under ThreadSanitizer in CI; assert no data races.
- [ ] Add a separate test that calls `stop()` from inside a handler thread and asserts deadlock-detection (or simply documents the timeout); skip the test by default in CI but make it runnable on demand to validate the contract.
- [ ] Document the deadlock case in `webserver::stop()` Doxygen.

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

**Status:** Not Started
