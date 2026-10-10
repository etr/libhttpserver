# Unworked Review Issues

**Run:** 2026-10-10 16:28:56
**Task:** TASK-194
**Total:** 1 (0 critical, 1 major, 0 minor)

## Major

1. [ ] **housekeeper** | `specs/architecture/v3/architecture.md:366` | architecture-not-updated
   The Public native certificate TLS section still states “Later tasks own public mTLS/PSK/ACME and H2/H3 integration.” TASK-194 implements and documents public initial mTLS, HTTP/1 external PSK, and ACME publication/removal, so the architecture overview contradicts the delivered component scope.
   *Recommendation:* Update the Public native TLS architecture section to record TASK-194’s public mTLS, PSK, and ACME responsibilities while keeping the documented H2/H3 deferral. Run groundwork-source-architecture-from-code to synchronize the architecture with the implementation.
