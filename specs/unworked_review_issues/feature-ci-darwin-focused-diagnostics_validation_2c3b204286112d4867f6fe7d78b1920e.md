# Unworked Review Issues

**Run:** 2026-10-23 18:57:57
**Task:** FEATURE-ci-darwin-focused-diagnostics
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **code-quality-reviewer** | `test/integ/external_http1_e2e_test.cpp:132` | diagnostic-isolation
   The diagnostic-only trace_header member and assignment, and the conditionals at lines 85, 91 and 108, remain in ordinary builds because only LHS_DARWIN_DIAG expands away. In an unoptimized build the fixture layout, flag initialization and conditional reads remain, falling short of the task requirement that normal-build tracing be fully compiled out.
   *Recommendation:* Guard the member, assignment and enclosing trace conditionals with LHS_DARWIN_FOCUSED_DIAGNOSTICS, or put the condition inside a macro that elides it when diagnostics are disabled. Keep the unchanged test assertions and deadlines.
