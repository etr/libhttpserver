# Unworked Review Issues

**Run:** 2026-10-08 07:12:27
**Task:** TASK-157
**Total:** 2 (0 critical, 0 major, 2 minor)

## Minor

1. [ ] **performance-reviewer** | `src/detail/quic_flow_control.cpp:54` | algorithmic-complexity
   Every stream lookup scans the retained record vector, including retired records. Receive/consume/commit paths perform repeated lookups, and constrained recovery selection performs one per eligible STREAM descriptor. Work therefore grows with connection lifetime records; with the supported 65536-record bound, an active stream near the end remains expensive even after earlier streams retire. This is bounded and does not block the declared foundation/composition scope.
   *Recommendation:* Benchmark receive and blocked prepare_packet at representative and maximum record counts. If large tables are intended, add a preallocated, budgeted ID-to-record index while preserving stable records, retired-ID rejection, and allocation-free commit.

2. [ ] **performance-reviewer** | `src/detail/quic_recovery.cpp:28` | algorithmic-complexity
   Critical packet cleanup erases individual elements from the sent vector inside its traversal. A stalled ordinary packet followed by many collectable critical packets causes repeated tail shifts, making this newly added compaction quadratic in the batch size. The 4096-packet hard ceiling and 256-packet default bound the cost, so this is non-blocking in the current foundation scope.
   *Recommendation:* Use a single stable compaction pass after successful packet-number retirement, retaining entries whose history admission fails. Benchmark mixed stalled ordinary and collectable critical batches at the default and maximum configured packet counts.
