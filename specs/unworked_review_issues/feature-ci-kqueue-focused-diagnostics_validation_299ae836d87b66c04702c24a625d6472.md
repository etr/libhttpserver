# Unworked Review Issues

**Run:** 2026-10-25 01:17:32
**Task:** FEATURE-ci-kqueue-focused-diagnostics
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **test-quality-reviewer** | `scripts/test-diagnostic-kqueue-batch.py:156` | missing-test
   The only batch-deadline test supplies batch_seconds=0, so it proves refusal before the first launch but does not exercise the required cumulative 300-second execution bound. The existing 20-second process-timeout test is independent. Regressions that stop subtracting elapsed batch time, or replace min(process_seconds, remaining) with process_seconds, would still satisfy these tests while allowing subsequent native execution beyond the approved batch budget.
   *Recommendation:* Add focused offline tests with controlled time/process boundaries to verify that elapsed time across a completed first attempt prevents a later launch when the batch budget is exhausted, and that a launch with less remaining batch time than the process budget receives only that remaining time. Keep all children fake and execute no C++ or native program.
