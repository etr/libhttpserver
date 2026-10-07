# Unworked Review Issues

**Run:** 2026-10-07 02:10:41
**Task:** TASK-136
**Total:** 1 (0 critical, 0 major, 1 minor)

## Minor

1. [ ] **code-simplifier** | `test/unit/tls_acme_peer.hpp:133` | code-structure
   corrupt_name and corrupt_alpn duplicate the ClientHello extension framing walk, including the fixed-field cursor setup and extension bounds checks. That duplication makes future fixture changes easy to apply to only one mutation helper.
   *Recommendation:* Extract the shared ClientHello extension traversal into a small helper that finds a requested extension and applies a mutation callback, then keep the SNI and ALPN byte changes in their focused callers.
