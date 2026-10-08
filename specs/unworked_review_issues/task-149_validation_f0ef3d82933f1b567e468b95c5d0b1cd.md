# Unworked Review Issues

**Run:** 2026-10-08 00:02:33
**Task:** TASK-149
**Total:** 4 (0 critical, 1 major, 3 minor)

## Major

1. [ ] **performance-reviewer** | `src/detail/io_connection_owner.cpp:65` | memory-allocation
   Owner admission charges packet.bytes.size(), but received packets retain the original vector capacity after resize. With the default 65,507-byte receive storage and a stalled owner, 64 four-byte packets are accepted with only 256 charged bytes yet retain 4,192,448 bytes against the default 1,048,576-byte byte limit. This remains count-bounded, but multiplies retained memory across connections and weakens the configured byte budget.
   *Recommendation:* Carry an immutable allocated-storage charge into the owner record (or charge vector capacity), use that charge for admission and release, and verify the real receive-to-dispatch path with small packets and stalled drains.

## Minor

2. [ ] **code-quality-reviewer** | `test/unit/io_udp_backend_contract.hpp:78` | test-quality
   The shared UDP contract accumulates send, receive, metadata, ordering, truncation, cancellation, and release checks into one boolean. A regression in any of these paths reports only the outer LT_CHECK(udp_contract<Backend>()) failure, losing the failing phase and expected/actual values, particularly for CI-only backends.
   *Recommendation:* Preserve the reusable backend contract while reporting named phase failures or individual assertions so a failing backend/family identifies the exact broken behavior.

3. [ ] **code-simplifier** | `src/detail/quic_datagram_dispatch.cpp:68` | code-structure
   The dispatch miss path constructs the same binary destination-CID key once for route lookup and again for retired-route lookup.
   *Recommendation:* Create the key once before the lock scope and use that local for both lookups; this removes a redundant allocation and keeps both checks visibly tied to the same CID.

4. [ ] **performance-reviewer** | `src/detail/io_iocp_backend.cpp:119` | algorithmic-complexity
   Every UDP submission scans all outstanding_ and pending_ entries under the shared backend mutex, including unrelated TCP sockets and timers. The socket's 64-operation cap does not bound this scan; its cost grows with the entire server's active operations. Current backend preparation already uses global scans, so this is an incremental efficiency concern rather than a demonstrated throughput blocker.
   *Recommendation:* Benchmark UDP admission under mixed TCP/UDP load on Windows CI. If material, keep count/byte reservations on the registration, retaining posted cancellation charges until their physical completion packet is consumed.
