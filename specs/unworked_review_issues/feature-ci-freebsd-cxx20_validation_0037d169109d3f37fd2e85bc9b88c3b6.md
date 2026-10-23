# Unworked Review Issues

**Run:** 2026-10-23 14:38:07
**Task:** FEATURE-ci-freebsd-cxx20
**Total:** 3 (0 critical, 2 major, 1 minor)

## Major

1. [ ] **architecture-alignment-checker** | `.github/workflows/v3-release-acceptance.yml:302` | interface-contract
   The selected SDK identity does not reach the release lane native checks. The accepted plan requires complete SDK selection in both package/archive paths while retaining all original lane assertions; architecture section 5 requires native packaging checks. toolchain.env contains unexported shell assignments, so sourcing it supplies values only to the explicit CLI arguments here. run-v3-release-ci.py platform_proof forwards --toolchain-prefix to its child package matrix, then executes gmake check using its original environment (lines 60 and 83). The child matrix cannot update the parent environment. A bounded invocation-boundary probe confirmed the matrix receives --toolchain-prefix but the later [gmake, check, -j2] environment has V3_PACKAGE_TOOLCHAIN=null. Makefile.am:289 includes check-v3-native-linkage, whose newly changed audit requires that variable for selected libc++ (scripts/audit-v3-native-linkage.sh:366). Consequently the archive lane fails with selected FreeBSD C++ runtime prefix missing after its matrix succeeds.
   *Recommendation:* Propagate the selected toolchain prefix to the release runner native-check environment, for example by exporting V3_PACKAGE_TOOLCHAIN before invoking the platform runner or deriving it from args.toolchain_prefix inside platform_proof. Add a focused regression that inspects the later make-check invocation as well as the matrix CLI; retain the strict audit assertion.

2. [ ] **test-quality-reviewer** | `scripts/test-v3-native-linkage.py:118` | test-isolation
   The default otool fixtures do not control uname, but audit-v3-native-linkage.sh now chooses its FreeBSD ldd branch before otool. On a FreeBSD host, AuditFixtures.audit and MakeGateFixtures.gate therefore run the real ldd against their shell-script fake binary instead of the intended inspector fixture. A bounded probe supplying uname=FreeBSD reproduces failures in AuditFixtures.test_freebsd_loader_is_platform_runtime and MakeGateFixtures.test_configured_executable_suffix_reaches_real_audit, with the latter reporting 'ldd failed'. This newly makes existing success tests host-dependent and also lets negative tests pass for the wrong reason.
   *Recommendation:* Set uname explicitly to Darwin for the default otool fixtures in both fixture builders, retaining explicit FreeBSD and Windows overrides. Verify the two affected success tests with an ambient uname reporting FreeBSD so fixture dispatch stays independent of the host.

## Minor

3. [ ] **housekeeper** | `docs/v3-ci.md:30` | documentation-stale
   The CI guide describes FreeBSD's Python venv setup but not the new private stable libc++20.1.8 SDK selector, pinned-source build, or cancellation/runtime-resolution gate now used by both FreeBSD package lanes. The task card documents these details, but the general CI operations guide is incomplete for the changed workflow.
   *Recommendation:* Update the FreeBSD toolchain section in docs/v3-ci.md to describe the private pinned libc++ SDK, the retained system libcxxrt ABI/runtime ownership, and the selector's cancellation probe; keep native FreeBSD post-repair execution identified as CI/v3-PR evidence until it runs.
