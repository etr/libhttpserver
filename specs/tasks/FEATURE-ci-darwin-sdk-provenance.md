# FEATURE-ci-darwin-sdk-provenance

Base: local v3 `251bfa8d747775e15a726b0b7300368fd72336ab`.
Status: implemented; scoped local verification passed; awaiting caller validation. Scope: exact selected Darwin SDK metadata in
packaged release compile proof; no runtime or other CI investigation.

Decision: identity owns precompile selection and byte pinning of the exact
Clang-selected SDKSettings.json, then verifies it and actual command context.
The independent A/B interface comparison is retained in
`.debug/ci-f498-darwin-python-assertions/design-comparison.md` in the primary
workspace; A concentrates pre/post trust binding in existing identity operations.
B attaches per-compile receipts but cannot alone prove precompile bytes.

Doubts (all open until durable evidence is recorded):
1. Forged arbitrary/private/same-hash sibling metadata must not gain authority.
2. SDK, driver, configured command files, environment or metadata mutation must fail.
3. Actual command flags/env must not change selected context after the CXX prefix.
4. Source/build/private and foreign/noncontributing archive gates must survive.
5. Raw dependency, metadata bytes, original query and artifact binding must survive.
6. Unsupported/ambiguous/no-SDK discovery grants nothing; old SDK-free proof stays valid.
7. Literal compound/quoted CXX, spaces and canonical aliases must remain usable.
8. Portable logic/real local Clang Darwin-driver dry runs are not native Darwin proof.

Verification scope: packaged adapter contracts and selected CanonicalFixtureContracts
public compile case, changed Python syntax/static checks. No broad baseline,
provider/SDK/client build, Windows launch, merge, publication or cleanup.
The original native failure and unrelated closed/unknown investigations remain in
the primary journal. Native Darwin compatibility awaits CI/v3 PR.

Baseline: `python3 scripts/test-v3-packaged-adapters.py`: 42 passed.
No project-specific agent memory was present under the agent directory.

Implementation complete (caller validation/merge still pending):
- [x] Exact selected SDKSettings context captured by existing identity entry point.
- [x] Live original and retained bytes, selected driver and literal compiler inputs
  bound before compilation and independently reverified afterward.
- [x] Actual CXX prefix, selection environment, cwd-dependent selection and
  bounded extra flags checked; indirect config/response/VFS/forwarding rejected.
- [x] Raw dependency/depfile evidence and all public/source/private/static gates
  retained; metadata artifacts required in identity selection/catalog.
- [x] Existing SDK-free receipt behavior preserved, without allowing legacy
  receipts to borrow a newly discovered postcompile context.
- [x] Helper distributed in source archive; optional internal contract documented.

Doubts resolved by durable tests in `scripts/test-v3-packaged-adapters.py`:
1. CLEARED: `test_sdk_context_cannot_override_foreign_source_or_static_contribution`
   rejects arbitrary/private/sibling metadata; `test_sdk_selected_root_and_environment_change_after_capture_fails`
   rejects a self-consistent forged same-hash SDK/query through independent selection.
2. CLEARED: `test_identity_captures_selected_sdk_before_any_consumer_compile`,
   `test_verify_identity_rejects_sdk_mutation_after_precompile_capture`, and
   `test_sdk_metadata_driver_and_configured_wrapper_bytes_are_pinned` cover pre/post bytes.
3. CLEARED: `test_sdk_proof_is_bound_to_actual_compiler_and_complete_selection_environment`,
   `test_sdk_proof_rejects_context_overrides_outside_compound_cxx_prefix`,
   `test_sdk_context_is_rechecked_in_actual_compile_working_directory`, and
   `test_sdk_discovery_rejects_indirect_configured_cxx_inputs_without_permission`.
4. CLEARED: existing source/private/build/header tests, the SDK-bearing static
   rejection/positive controls, and the existing real unused/selected archive test
   (normal and quoted compound CXX) retain all original gates.
5. CLEARED: `test_selected_sdk_metadata_preserves_complete_depfile_and_positive_compile`,
   `test_retained_sdk_query_tampering_is_rejected`,
   `test_sdk_missing_pruned_tampered_and_unproved_artifacts_fail`,
   `test_identity_artifacts_require_selected_sdk_bytes_and_driver_inputs`, and
   `test_sdk_artifact_catalog_retains_original_metadata_bytes` cover retained truth/binding.
6. CLEARED: `test_sdk_discovery_rejects_escaped_metadata_and_ambiguous_or_failed_queries`,
   legacy SDK-free identity revalidation, and legacy benchmark no-borrow tests.
7. CLEARED: quoted compound fixture selection, direct selected driver, cwd and
   canonical SDK alias tests plus selected symlinked-TMPDIR integration.
8. CLEARED AS LOGIC SCOPE: `test_local_clang_darwin_driver_dry_run_selects_exact_controlled_sdk`
   runs local Clang's Darwin-target driver only. No target code runs. Native
   Darwin compatibility is explicitly **not verified** and remains CI/v3-PR owned.
No open logical trust doubt remains. This retains the existing trusted compiler
and logger boundary; it makes no claim to authenticate malicious compilers or
transient mutations between observations.

TDD observations: the initial regression failed with `private or foreign dependency`
where the legitimate selected metadata needed to reach the `static archive`
rejection. Subsequent REDs caught missing precompile identity/artifact/env
integration, tampered query acceptance, raw diagnostic identity instability,
cwd-dependent selection drift, legacy context borrowing, and indirect forwarded
prefix inputs. Each was followed by scoped GREEN verification. Other negative
controls verify that existing rejection gates remain intact.

Local verification: packaged adapter module **65 passed**; selected
`CanonicalFixtureContracts.test_public_compile_and_receipts_through_symlinked_tmpdir`
**1 passed**; changed Python `py_compile` and `git diff --check` passed.
The repository defines cpplint for C++; no C++ file changed and no Python linter
is configured. Evidence is retained in the primary workspace under
`.debug/ci-f498-darwin-python-assertions/implementation/` (including exact local
Clang -### output and context, scoped check logs and a TDD transcript summary).

Original f498 native failure remains retained. No post-fix native Darwin result,
full baseline, provider/SDK/client build or native Windows probe was claimed.
The parent's independently queued Windows temp-parent identity fix and closed
Mac TLS static/external EOF timeout are outside this task. Existing unrelated
header/WSAPoll/postsummary/clock/kqueue25/epoll64 decisions remain unchanged.
