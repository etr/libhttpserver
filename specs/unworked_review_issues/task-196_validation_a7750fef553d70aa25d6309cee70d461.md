# Unworked Review Issues

**Run:** 2026-10-11 00:35:26
**Task:** TASK-196
**Total:** 2 (0 critical, 1 major, 1 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/integ/http3-client-go/public.go:310` | missing-test
   The independent destruction witness reports passed for every publicRead error. publicOpen sets a three-second stream deadline, so a server that leaves the held stream unresolved and never closes or resets it still produces a timeout and reaches status=passed. Protocol/decoder errors are also accepted. This weakens TASK-196’s active-stream destruction/cancellation witness; the stricter aioquic test does not make this independent-client predicate valid.
   *Recommendation:* Require an observed remote QUIC connection-close or stream cancellation with the expected shutdown semantics; reject deadline/context timeout and decoding errors. Add a bounded negative witness using a peer that keeps the stream open without closing/resetting it, and require that witness to fail rather than emit a passing destruction result.

## Minor

2. [ ] **performance-reviewer** | `src/detail/quic_network_packets.cpp:51` | memory-allocation
   Every receive_packet call value-initializes two 65,536-byte automatic buffers, clearing 128 KiB on the packet hot path even though the production UDP reader caps datagrams at 2,048 bytes. This is bounded and does not block basic routing, but adds avoidable stack footprint and memory traffic, including for failed authentication.
   *Recommendation:* Measure this cost in TASK-198/199 packet-throughput profiles. If material, right-size the buffers to an explicit accepted-datagram bound or reuse owner-serialized scratch storage, preserving capacity rejection, authenticated output lengths, and the crypto scratch wiping contract.
