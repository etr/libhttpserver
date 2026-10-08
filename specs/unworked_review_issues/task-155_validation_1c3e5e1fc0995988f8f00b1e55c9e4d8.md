# Unworked Review Issues

**Run:** 2026-10-08 04:50:27
**Task:** TASK-155
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/quic_tls_session_test.cpp:124` | logic-in-test
   The seven secret-error scenarios are synthesized by a numeric loop and six conditional mutations (lines 124-141); callback_failures_are_terminal_and_zero_capacity_retries repeats numeric branching at lines 242-259. Each test mixes scenario selection, setup, and the operation being checked, so a new or mistyped failure index can silently exercise the default case and failed checks report the same test name rather than the failing scenario.
   *Recommendation:* Replace numeric failure dispatch with explicit named cases carrying level/direction/suite/length and any prerequisite secret installation. Give the four callback failure operations separate named tests or a descriptor table with a named operation, retaining the shared assertions for terminal state.
