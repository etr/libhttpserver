# Unworked Review Issues

**Run:** 2026-10-23 23:56:07
**Task:** FEATURE-ci-freebsd-readelf-format
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **security-reviewer** | `scripts/audit-v3-installed-package.py:206` | input-validation
   The new whitespace-excluding name pattern silently drops a recognized NEEDED entry whenever another entry matches. A bounded real-inspector probe with GNU rows for libc.so.6 and libunexpected name.so returns only libc.so.6, whereas the original parser raises missing resolution for libunexpected name.so. The omitted library is never visited, hashed or checked by graph_violations. This violates the accepted fail-closed recursive graph requirement and can conceal an undeclared runtime dependency (CWE-20).
   *Recommendation:* Fail closed when a recognized NEEDED row cannot be completely parsed or resolved; unsupported names need not be accepted, but must not disappear from an otherwise successful inspection. Add a mixed valid/unsupported NEEDED regression verifying rejection rather than a partial dependency list.
