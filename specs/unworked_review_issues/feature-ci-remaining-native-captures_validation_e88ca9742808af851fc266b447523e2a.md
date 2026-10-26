# Unworked Review Issues

**Run:** 2026-10-25 18:06:45
**Task:** FEATURE-ci-remaining-native-captures
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **housekeeper** | `specs/tasks/FEATURE-ci-remaining-native-captures.md:103` | documentation-stale
   The verification summary identifies the three rebuilt objects by compiler and source, and says unchanged TLS/wire receipts are reused, but it does not state the recorded count of 10 reused compile receipts from repair-verification-iter1.json. The task card therefore omits the requested affected/reused compile counts.
   *Recommendation:* State explicitly that 3 affected object compiles passed and 10 unchanged compile receipts were reused.
