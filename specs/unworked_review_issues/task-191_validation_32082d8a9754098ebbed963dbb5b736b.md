# Unworked Review Issues

**Run:** 2026-10-10 12:26:56
**Task:** TASK-191
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **code-simplifier** | `test/integ/http3-client-go/semantic_test.go:9` | dependencies
   The third-party quic-go import is interleaved with standard-library imports, making the dependency boundary less clear and differing from the usual Go import grouping.
   *Recommendation:* Place the standard-library imports together, add a blank line, then place github.com/quic-go/quic-go/logging in a separate group.
