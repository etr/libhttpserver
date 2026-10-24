# Unworked Review Issues

**Run:** 2026-10-23 20:59:32
**Task:** manual-validation
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **code-simplifier** | `scripts/test-v3-installed-package.py:513` | code-structure
   The metadata-failure test catches Exception and then separately checks that it is AuditError, making the expected failure less explicit.
   *Recommendation:* Use self.assertRaises(audit.AuditError) directly and keep the message and retained-command assertions.
