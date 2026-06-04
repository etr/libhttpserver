### TASK-085: Residual test-smell sweep

**Milestone:** M7 - v2 Cleanup
**Component:** `test/Makefile.am`, `test/unit/`
**Estimate:** S

**Goal:**
A cluster of small, independent test smells that the audit groups under "Other test smells". Each is too small to be its own task but together they erode confidence in the test contract.

**Action Items:**
- [ ] `test/Makefile.am:67-74`: orphaned comment claiming `header_hygiene` is in `XFAIL_TESTS`; lines 532-535 admit it was removed when TASK-020 landed. Update the comment. (Mechanical — overlaps with TASK-061; do whichever lands first and drop from the other.)
- [ ] `test/unit/auth_handler_optional_signature_test.cpp:176-192`: `throwing_handler_is_swallowed_and_request_passes` ratifies a questionable swallow-and-pass behaviour as the pin. Decide: is this the intended contract? If yes, update the test name to make the contract explicit (e.g., `auth_handler_exception_is_logged_and_request_proceeds_without_auth`) and document in `specs/architecture/` why. If no, change the behaviour to fail-closed (reject the request with 500 or equivalent) and update the test accordingly.
- [ ] `test/unit/hooks_log_access_alias_slot_test.cpp:166-205`: "second registration replaces first" only simulates re-registration via two webservers. Once TASK-066 ships the runtime setter (or pins the immutable-after-start contract), update this test to exercise the real path.
- [ ] `test/unit/webserver_register_smartptr_test.cpp:60-64, 148-156`: documented parallel-runner fragility. Diagnose root cause (most likely a shared static or filesystem path collision) and fix, or convert the affected sub-tests to serial-only with an explicit littletest annotation.

**Dependencies:**
- Blocked by: TASK-066 (for the alias-slot test work)
- Blocks: None

**Acceptance Criteria:**
- Each test in the action list has either a behaviour fix or a documentation update naming the intended contract.
- Parallel test runner is green on every CI lane.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 test reliability NFR
**Related Decisions:** None new

**Status:** Backlog
