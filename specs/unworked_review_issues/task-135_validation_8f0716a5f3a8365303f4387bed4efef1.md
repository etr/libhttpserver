# Unworked Review Issues

**Run:** 2026-10-07 01:02:11
**Task:** TASK-135
**Total:** 2 (0 critical, 2 major, 0 minor)

## Major

1. [ ] **performance-reviewer** | `src/detail/tls_io_backend.cpp:353` | blocking-io
   The handshake worker holds delivery->mutex across self->enqueue(), which may synchronously invoke the owner executor and resume application code. close() takes the same mutex at line 485 before invalidating delivery or requesting stop, so teardown waits arbitrarily for that application continuation. This violates the external-PSK contract that owner timeout/close remain independently runnable and do not hide unbounded retirement waits. A current-archive actual-adapter probe with shipped inline_executor, without simultaneous post calls, queued a 20 ms handshake behind a held worker, let it expire, and held its resumed terminal continuation. A separate close() stayed blocked for 100 ms and returned only after releasing that continuation; output was completion_code=8, close_blocked_100ms=1, drained=1. Probe source: /private/tmp/task135-performance-inline.cpp; binary: /private/tmp/task135-performance-inline; exit code: 0. Current build-on/src/.libs/libhttpserver_v3tls.a and libhttpserver_v3core.a; selected OpenSSL 3.5.9 libraries.
   *Recommendation:* Make completion lifetime arbitration independent of synchronous execution of owner/application handlers so close can invalidate delivery promptly. Preserve safe executor retirement and late-result handling while ending the gate critical section before application work executes; add a bounded inline-executor regression with a held terminal continuation and prompt close.

2. [ ] **test-quality-reviewer** | `test/unit/tls_psk_fixture.hpp:76` | missing-test
   Every adapter connection replaces its handshake operation with an explicit 2-second deadline (or a shorter supplied deadline), while the runtime retains its default 5-second handshake timeout. No added test sets a valid shorter runtime timeout or submits a PSK handshake without an explicit operation deadline. Consequently the runtime cap in src/detail/tls_io_backend.cpp:278 is never the winning deadline: removing that cap would leave the existing tests passing while an operation using its default infinite deadline could hold a lookup indefinitely. This misses the finite runtime deadline contract in docs/external-psk-lookup-contract.md:215-217.
   *Recommendation:* Add a focused adapter case with a short valid runtime handshake_timeout and an omitted operation deadline, holding lookup until after logical timeout. Observe the finite callback deadline, timeout completion while lookup remains held, retained capacity/incomplete drain, and safe late retirement. Also cover a later explicit operation deadline so the runtime policy wins; preserve the existing earlier-operation-deadline case.
