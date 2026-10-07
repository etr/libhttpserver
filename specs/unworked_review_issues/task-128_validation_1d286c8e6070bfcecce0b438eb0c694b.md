# Unworked Review Issues

**Run:** 2026-10-06 20:41:59
**Task:** TASK-128
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **performance-reviewer** | `src/httpserver/detail/http1_body_source.hpp:160` | algorithmic-complexity
   Every data pull invokes room_released_, including pulls from a fully decoded body and pulls when no reader is backpressured. The engine callback calls backend_.wake(); both io_poll_backend::wake and io_managed_socket_backend::wake scan all pending operations and complete every parked wake operation across connections. Small application read buffers therefore add a global scan and unrelated loop resumptions per pull, rather than per actual reader admission transition. The single-connection plateau tests do not measure this throughput cost.
   *Recommendation:* As a follow-up, profile small-buffer uploads with many idle connections. If material, suppress notifications when no reader admission transition is needed, while preserving the engine-mutex registration ordering and tail-ready retry that close the lost-wake race; no backend redesign is required for TASK-128 approval.
