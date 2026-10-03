# Unworked Review Issues

**Run:** 2026-10-03 16:22:13
**Task:** TASK-122
**Total:** 2 (0 critical, 1 major, 1 minor)

## Major

1. [ ] **security-reviewer** | `src/httpserver/exchange.hpp:331` | thread-safety
   CWE-362: upgrade publishes the WebSocket driver/stream phase inside http1_exchange_sink::on_upgrade before exchange::upgrade changes its ordinary non-atomic state_ to upgraded. A peer can send a valid opening and an invalid first frame in the same socket write. The reader then feeds that staged frame and the new terminal progress observer calls disconnect_current / exchange::disconnect concurrently with the upgrade state write. A temporary probe using the existing two-worker native socketpair rig, an ordinary upgrade-then-receive handler, and coalesced HTTP-looking invalid frame bytes reproduced a ThreadSanitizer read/write race between exchange::disconnect and exchange::upgrade, with halt_on_error exit 134. The engine mutex protects disconnect but is not held by the post-on_upgrade state write. This violates the single-terminal-decision/thread-safe cancellation contract and constitutes C++ undefined behavior. Evidence is preserved beside this artifact in task122-security-terminal-probe.cpp and task122-security-terminal-probe.log; the probe linked the matching fresh native TSan archive. No concrete production crash or corruption was established.
   *Recommendation:* Serialize the successful exchange state transition with driver/reader publication and terminal exchange mutation, preserving nonterminal refusal and avoiding invocation of protocol notifications while holding the session mutex. Do not rely only on cancellation posting: the observer already directly reads the exchange state. Retain the coalesced malformed-first-frame case as a native multiworker TSan regression, and verify close/abort/timeout still notify once without a second HTTP decision.

## Minor

2. [ ] **test-quality-reviewer** | `test/unit/http1_websocket_upgrade_test.cpp:117` | missing-test
   The route-to-connection limit projection in src/detail/connection_engine_websocket.cpp:40-48 is not exercised with a connection limit lower than route/default limits. Existing session_options_projection tests validate from_budgets and the native fixture tightens route limits, but neither checks the adapter boundary. Source inspection shows straightforward minimum clamping, so this is optional regression coverage rather than evidence of a production defect.
   *Recommendation:* Add a focused adapter case with a lower connection message/queue budget and larger valid route options; assert an oversized send is rejected and queue admission/backpressure honors the connection cap. Retain a route-tightened case if it tests a distinct observable boundary.
