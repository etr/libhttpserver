# Unworked Review Issues

**Run:** 2026-10-09 01:42:11
**Task:** TASK-173
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **test-quality-reviewer** | `test/unit/quic_interop_files_test.cpp:198` | coverage-completeness
   The eight-slot pipeline regression acknowledges and completes all eight original records before cancellation, then cancels only the one refill record. The other cancellation test also retains at most one record per stream. Consequently the changed cancellation loop over all occupied slots has no regression proving that a cancellation with several outstanding chunks releases every retained record.
   *Recommendation:* Add a bounded case that fills all eight pending slots, cancels before their completions, and checks that no STREAM remains eligible and retained resource charges return to their pre-retention level.
