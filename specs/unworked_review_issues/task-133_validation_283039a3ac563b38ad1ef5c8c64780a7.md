# Unworked Review Issues

**Run:** 2026-10-06 23:37:32
**Task:** TASK-133
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/tls_mtls_test.cpp:166` | missing-test
   The concurrent metadata reader has no start coordination or observation assertion. The main thread can finish peer.connect() and set stop=true before the reader executes, or before it ever reads a nonnull pointer. In either schedule errors remains zero and lines 176-179 pass using only main-thread checks. Thus the planned TSan/concurrency coverage of metadata publication can pass without exercising its new thread-safe read contract.
   *Recommendation:* Coordinate reader startup before driving the handshake, and require the reader itself to observe and validate a published identity before it exits. Use explicit synchronization and bounded failure handling so the test proves pre-publication and post-publication reads without relying on scheduling or sleeps; retain the existing independent final-state and lifetime assertions.
