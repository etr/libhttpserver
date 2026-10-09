# Unworked Review Issues

**Run:** 2026-10-08 23:30:27
**Task:** TASK-172
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **performance-reviewer** | `src/detail/http3_response_stream.cpp:164` | memory-allocation
   Each new WebSocket DATA section allocates and value-initializes response_buffer_bytes of temporary payload storage, then copies the actual output into another vector. With the default 16 KiB buffer, even a two-byte empty message or a small Pong allocates/zeros 16 KiB on this output hot path. This is bounded and does not block acceptance, but causes avoidable allocator and memory traffic for control-heavy sessions.
   *Recommendation:* Reuse a bounded staging buffer or copy into the final DATA staging storage while preserving the driver's copy_output/consume_output pinning contract. Measure small-message/control-frame throughput before adopting a more complex layout.
