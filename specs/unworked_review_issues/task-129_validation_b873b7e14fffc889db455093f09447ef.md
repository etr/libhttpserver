# Unworked Review Issues

**Run:** 2026-10-06 21:15:43
**Task:** TASK-129
**Total:** 3 (0 critical, 1 major, 2 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/native_server_test.cpp:406` | missing-test
   Every HTTP/2 and HTTP/3 variant also adds a TLS listener. Removing the independent protocol-set rejection in src/detail/server.cpp:130-135 would leave these cases passing through the listener.tls rejection. Therefore the new requirement to reject unavailable HTTP/2 and HTTP/3 before listening has no test for a semantically valid configuration with only plaintext listeners and a selected provider.
   *Recommendation:* Add isolated HTTP/2 and HTTP/3 cases with only plaintext listeners, retain HTTP/1 in the protocol set and the selected certificate provider, and assert validate().ok(), listen() returns not_supported, is_running() is false, and bound ports remain zero. Keep a separate mixed-listener TLS rejection case.

## Minor

2. [ ] **code-quality-reviewer** | `test/unit/native_server_test.cpp:408` | test-coverage
   Every iteration of unsupported_transports_rejected_before_any_bind adds a TLS listener. Removing the HTTP/2 and HTTP/3 guard in prelisten_ready() would still leave these cases rejected by the TLS-listener guard, so they do not independently protect protocol rejection for a semantically valid plaintext listener configuration with a selected provider.
   *Recommendation:* Add HTTP/2 and HTTP/3 cases with only a plaintext listener, retaining the default HTTP/1 protocols and selected certificate provider so options.validate() succeeds, then assert not_supported and no bound endpoint. Keep the existing mixed-listener case for TLS rejection before any bind.

3. [ ] **code-simplifier** | `test/Makefile.am:1699` | patterns
   The same provider linker flag expression is repeated for roughly forty test programs. That makes the native provider linkage requirement harder to maintain and easy to omit when another test later links the native core.
   *Recommendation:* Consider adding `$(V3_TLS_LDFLAGS)` once to the test directory's common `AM_LDFLAGS`, then remove the per-program copies. Confirm the resulting test link commands still keep the provider libraries and their search paths scoped as intended.
