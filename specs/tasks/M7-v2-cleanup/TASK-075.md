### TASK-075: Propagate `wait_for_server_ready` to sibling hooks integ tests

**Milestone:** M7 - v2 Cleanup
**Component:** `test/integ/hooks_*`
**Estimate:** M

**Goal:**
TASK-049 finding #3 introduced `wait_for_server_ready` in
`test/integ/hooks_handler_exception_chain.cpp:53-71` but the fix was never
propagated to ~20 sibling integ tests, which still use bare
`std::this_thread::sleep_for(50ms)` as a server-ready wait. The audit
identifies this as the single largest piece of consistent deferred work.
Replace every occurrence in the listed files.

**Action Items:**
- [x] Extract `wait_for_server_ready` from `hooks_handler_exception_chain.cpp` into a shared header (e.g. `test/integ/server_ready.hpp`) so the helper has one home.
- [x] Replace `std::this_thread::sleep_for(std::chrono::milliseconds(50))` with `wait_for_server_ready(ws)` in each of the following files (line numbers from the audit, verify against current HEAD):
  - `test/integ/hooks_after_handler_mutates_response_in_place.cpp:77`
  - `test/integ/hooks_after_handler_replaces_response.cpp:72`
  - `test/integ/hooks_accept_decision_throwing.cpp:93, 127`
  - `test/integ/hooks_accept_decision_banned.cpp:90`
  - `test/integ/hooks_body_chunk_short_circuit_no_leak.cpp:90`
  - `test/integ/hooks_body_chunk_observes_progress.cpp:88`
  - `test/integ/hooks_before_handler_short_circuit.cpp:82, 128`
  - `test/integ/hooks_alias_functional_test.cpp:105`
  - `test/integ/hooks_handler_exception_user_handler_throws_continues_chain.cpp:92`
  - `test/integ/hooks_handler_exception_fallback_to_hardcoded_500.cpp:108`
  - `test/integ/hooks_connection_lifecycle.cpp:126`
  - `test/integ/hooks_per_route_early_413_per_endpoint.cpp:132`
  - `test/integ/hooks_per_route_concurrent_registration.cpp:120`
  - `test/integ/hooks_request_completed_fires_on_early_failure.cpp:95`
  - `test/integ/hooks_no_firing.cpp:151`
  - `test/integ/hooks_request_received_short_circuit.cpp:105, 163`
  - `test/integ/hooks_response_sent_carries_status_bytes_timing.cpp:79`
  - `test/integ/hooks_route_resolved_miss_and_hit.cpp:132`
  - `test/integ/hooks_per_route_order.cpp:96`
- [x] Run the changed suite under CI to confirm no flakiness regression on slow runners.
- [x] Add a grep-based regression check to `scripts/` that fails CI if a bare `std::this_thread::sleep_for(.*ms)` as a server-ready wait reappears in `test/integ/hooks_*`.

**Dependencies:**
- Blocked by: TASK-049 (Done; introduced the helper)
- Blocks: None

**Acceptance Criteria:**
- `grep -nE 'std::this_thread::sleep_for' test/integ/hooks_*.cpp` returns only sleeps with non-server-ready intent (documented in a comment).
- All `hooks_*` integ tests still pass on every CI lane.
- Grep regression check is wired into `make check` or `verify-build.yml`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD §2 test reliability NFR
**Related Decisions:** None new

**Status:** Done
