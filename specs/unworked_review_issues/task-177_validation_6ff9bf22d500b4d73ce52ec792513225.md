# Unworked Review Issues

**Run:** 2026-10-09 10:48:43
**Task:** TASK-177
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **code-simplifier** | `test/fuzz/http3_engine_fuzz.cpp:141` | code-structure
   The HEADERS observation branch constructs the same profile limits twice to check field count and expanded bytes.
   *Recommendation:* Store the header limits in a local within the HEADERS branch and use it for both checks, so they share one clearly named configuration value.
