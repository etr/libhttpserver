# Unworked Review Issues

**Run:** 2026-10-15 16:41:15
**Task:** FEATURE-gate-investigation
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **test-quality-reviewer** | `scripts/test-v3-installed-consumer.py:359` | implementation-coupling
   The newly observed member regression invokes the private _CommandTree directly and asserts at least four /proc scans. Four scans encode the current two-consecutive-retired-snapshots algorithm rather than the required outcome that cleanup cannot complete while a newly observed member is live. A behavior-preserving retirement strategy with a different observation sequence would fail this assertion.
   *Recommendation:* Exercise the retirement contract through Commands where practical, and use controlled OS observation state to assert that completion occurs only after the injected member becomes resource-retired. Keep the zombie success assertion, but remove the minimum scan-count requirement so the test protects retirement behavior independently of the barrier algorithm.
