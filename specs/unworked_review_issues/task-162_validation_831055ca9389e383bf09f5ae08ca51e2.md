# Unworked Review Issues

**Run:** 2026-10-08 13:05:42
**Task:** TASK-162
**Total:** 3 (0 critical, 1 major, 2 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/quic_repacketize_test.cpp:50` | missing-test
   The only test of the new reliable-control APIs constructs default server recovery and retains STOP_SENDING on valid stream 0. No test covers retain_handshake_done rejecting the client role or retain_stop_sending rejecting an illegal unidirectional stream direction or stream/error above k_quic_max_integer (src/detail/quic_repacketize.cpp:85-96). Removing or reversing these protocol guards would leave the added tests and both server-side smoke matrices green.
   *Recommendation:* Add focused cases through the recovery API for client-role HANDSHAKE_DONE rejection, legal/illegal STOP_SENDING directions for both endpoint roles, and max/max+1 stream/error boundaries. Assert invalid outcomes and that rejected retention does not queue information; retain a valid control afterward to verify usable state.

## Minor

2. [ ] **code-simplifier** | `scripts/run-v3-http3-gates.py:192` | readability
   The runner packs several state mutations onto single lines (for example initialization at line 192, receipt-and-break at line 205, and reset/ack/deadline at line 209). The same pattern appears in capture parsing and process cleanup. This makes the otherwise small fail-closed state machine harder to inspect for ordering and cleanup behavior.
   *Recommendation:* Expand compound statements into separate lines and use normal spacing, keeping the current event protocol, bounds, lifecycle ordering, and validation semantics. This is optional cleanup and does not block TASK-162.

3. [ ] **security-reviewer** | `test/integ/http3-client-go/go.mod:5` | vulnerable-components
   The test adapter pins quic-go v0.55.0, within the affected ranges for published HTTP/3 QPACK header and trailer memory-allocation advisories (CWE-770). Upstream documents CVE-2025-64702 affecting versions through v0.56.0 and CVE-2026-40898 affecting versions through v0.59.0. This is non-blocking here: the adapter verifies a newly generated private fixture CA and talks only to the bounded in-tree loopback server, so no hostile authenticated peer is exposed in the declared task operating model.
   *Recommendation:* When refreshing the independent test-client pin, choose a version with both fixes (at least v0.59.1), update the matching runner identity pin and lockfile, and rerun both smoke matrices. Maintain the current private fixture trust and loopback-only scope meanwhile.
