# Unworked Review Issues

**Run:** 2026-09-29 14:15:36
**Task:** TASK-097: Define public semantic types and ordered fields
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **comprehensive-review** | `test/unit/v3_header_hygiene_test.cpp:90` | test-enhancement
   The hygiene sentinel scans for config conditionals and backend/socket tokens but not for include-guard collisions between the six v3 headers and the existing v2 public headers (the exact defect class of finding 1). A regression would not be caught by tests.
   *Recommendation:* Add a hygiene assertion that every SRC_HTTPSERVER_*_HPP_* guard defined by the v3 headers is unique across src/httpserver.
