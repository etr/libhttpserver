# Unworked Review Issues

**Run:** 2026-10-14 23:04:10
**Task:** TASK-185
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **code-simplifier** | `scripts/run-v3-release-gates.py:49` | patterns
   This file repeats the chunked SHA-256 implementation already exposed as `sha256` by `scripts/lib/v3_release_artifact.py`.
   *Recommendation:* Import the artifact helper and delegate `digest(path)` to `release_artifact.sha256(path)` so artifact hashing has one implementation.
