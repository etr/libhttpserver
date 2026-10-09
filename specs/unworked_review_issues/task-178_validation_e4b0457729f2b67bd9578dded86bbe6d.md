# Unworked Review Issues

**Run:** 2026-10-09 13:48:51
**Task:** TASK-178
**Total:** 3 (0 critical, 3 major, 0 minor)

## Major

1. [ ] **code-quality-reviewer** | `scripts/run-v3-http3-full-gates.py:165` | error-handling
   run_process retires the process group only while its leader is still running. A leader that exits zero while a child inherits stdout is marked passed even though the command group continues writing. A bounded temporary probe spawned a child that slept 0.3 seconds then printed 1000 bytes: with limit=100, run_process returned passed at 0.052 seconds with six bytes, and stdout subsequently grew to 1007 bytes. This defeats this helper's output bound and permits a supposedly completed stage's artifacts to continue changing; the current successful evidence itself has no demonstrated orphan.
   *Recommendation:* Retire/check the owned process group even after leader exit, fail the stage if descendants remain, and preserve the original failure while recording cleanup failure. Add a bounded descendant probe that asserts no inherited-log writer survives completion and the receipt cannot pass while a child remains.

2. [ ] **performance-reviewer** | `scripts/run-v3-http3-full-gates.py:165` | resource-leak
   run_process only terminates a process group while the leader is still running. A successful leader can leave descendants alive after returning a passed receipt, bypassing the stage deadline and allowing inherited logs to grow after the final size check. The scoped reproducer started a child that slept 0.3 seconds then wrote 1,000 bytes with a 100-byte limit: run_process returned passed after 0.052 seconds with 6 log bytes, then the log grew to 1,007 bytes. This contradicts the task-local bounded execution/cleanup contract and can retain background processes or growing log files between release stages.
   *Recommendation:* Track and retire surviving members of the owned process group when the leader exits, and reject or fully reap unexpected descendants before accepting a passed receipt; verify the final log bound only after writers are stopped. Add a focused regression where the successful leader leaves a delayed stdout-writing child.

3. [ ] **test-quality-reviewer** | `test/integ/http3_full_gate_runner_test.py:138` | test-coverage
   test_network_rejects_wrong_matrix_missing_clients_failed_cleanup_and_identity passes identities={} for every receipt. The task174 missing-client row and the websocket wrong-version row therefore fail at fixture identity missing/changed (runner line 211), before their intended client/pin checks. No row reaches HTTP/WebSocket exit cleanup or source/hash validation, and there is no accepted validate_network baseline in this suite. The test remains green if these aggregate-layer checks disappear, despite its claimed coverage of independent-client identity and cleanup.
   *Recommendation:* Create a minimal accepted receipt/artifact fixture for each adapter, with matching fixture/binary/source identities and mocked predecessor wire validators where needed. Mutate one field at a time for missing client, wrong pin, cleanup exit/readers, receipt identity and source identity, asserting the targeted failure message and successful baseline first.
