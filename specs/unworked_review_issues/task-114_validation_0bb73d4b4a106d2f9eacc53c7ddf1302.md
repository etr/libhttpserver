# Unworked Review Issues

**Run:** 2026-10-01 23:30:32
**Task:** TASK-114
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **validator** | `src/Makefile.am:41` | build/dist-convention
   The six new header-only detail headers (base64/sha1/md5/sha256/secure_compare/entropy_sys.hpp) are not listed in noinst_HEADERS, so a 'make dist' tarball would not ship them even though detail/auth_basic.cpp includes them. Identical omission pre-exists at base b63f5bb for the http1_* header-only detail headers (http1_response_framer.hpp etc.), so this follows the established convention; fixing all of them is scope expansion for this task.
   *Recommendation:* Track as a follow-up to add all header-only v3 detail headers to noinst_HEADERS in one sweep.
