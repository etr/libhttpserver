# Unworked Review Issues

**Run:** 2026-10-15 12:15:33
**Task:** FEATURE-v3-protocol-limits
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **conventions-reviewer** | `ChangeLog:1` | workflow-violation
   The new public protocol capacity configuration and inspection API is user-facing, but no ChangeLog entry was added. CONTRIBUTING.md:132 requires all pull requests with user-facing changes to update ChangeLog, except for documentation-only and CI-only changes; this change adds public API and implementation files.
   *Recommendation:* Add a tab-indented one-line summary under the first Version 3.0.0 heading in ChangeLog, following CONTRIBUTING.md:134-139.
