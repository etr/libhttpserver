# Unworked Review Issues

**Run:** 2026-10-10 01:14:36
**Task:** TASK-183
**Total:** 2 (0 critical, 1 major, 1 minor)

## Major

1. [ ] **performance-reviewer** | `examples/v3_websocket_echo.cpp:28` | algorithmic-complexity
   The unbounded retry loop can consume a worker continuously for a slow-reading peer. writable() guarantees space only for an empty frame (session.hpp:54-57; websocket_session_state.cpp:44-45), so it returns immediately when >=2 bytes remain even though the same larger echo cannot fit. With default limits, three queued 1 MiB frames leave less capacity than a fourth needs; a bounded probe completed 1000 backpressured retry/writable cycles without any output consumption. The small-frame integration smoke does not exercise this state.
   *Recommendation:* For this simple echo example, set ws_upgrade_options.limits.outgoing_messages to 1 before upgrade so writable waits until the prior data frame fully drains and the next maximum-size message fits. Add a bounded example-focused test that queues a large message, presents another before draining, and verifies suspension rather than immediate retry progress. Keep the existing public session API unchanged.

## Minor

2. [ ] **test-quality-reviewer** | `test/integ/v3_examples_test.py:91` | missing-test
   The echo smoke exercises binary, exact-cap and oversized POST bodies, but neither it nor the semantic coroutine tests exercises the separate empty-body branch at examples/v3_route_handlers.hpp:45, which commits through respond() instead of start_response()/writer().
   *Recommendation:* Add a zero-length POST /echo assertion expecting status 200, Content-Length 0 and an empty response body; the semantic table can also cover body_size=0 if desired.
