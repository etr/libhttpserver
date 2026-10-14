# Unworked Review Issues

**Run:** 2026-10-13 20:35:41
**Task:** TASK-197
**Total:** 3 (0 critical, 1 major, 2 minor)

## Major

1. [ ] **code-simplifier** | `scripts/test-v3-public-http3.py:41` | code-structure
   The matrix fixture builds every scenario payload through one deeply nested conditional expression on a single line. This hides the mapping between a case and its expected fields, making later contract additions and corrections difficult to review.
   *Recommendation:* Extract named helpers for the distinct payload families (lifecycle, WebSocket, semantic parity, credentials, and mTLS), then keep the matrix comprehension focused on assembling cells. Preserve the existing expected values and scenario coverage.

## Minor

2. [ ] **code-quality-reviewer** | `scripts/test-v3-public-http3.py:41` | readability
   Contracts.matrix builds every configuration family in one long expression containing nested comprehensions and chained conditional expressions. The added lifecycle, credential, mTLS and WebSocket branches make the otherwise useful fail-closed test fixture difficult to inspect and extend independently.
   *Recommendation:* Optionally extract a configuration_row(case) helper with explicit family branches and small observation helpers, then keep matrix() responsible only for assembling cells. Preserve the existing malformed-receipt tests.

3. [ ] **test-quality-reviewer** | `scripts/test-v3-public-http3.py:35` | excessive-setup
   Contracts.matrix constructs all nested semantic, credential, QPACK, flow and WebSocket observations in one long conditional comprehension, making the starting valid receipt difficult to inspect when a negative contract case fails.
   *Recommendation:* Split the valid receipt construction into small named family builders and compose each cell explicitly, retaining the existing targeted mutations and assertions.
