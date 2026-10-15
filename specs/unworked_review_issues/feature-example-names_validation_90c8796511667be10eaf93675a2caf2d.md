# Unworked Review Issues

**Run:** 2026-10-15 14:29:50
**Task:** FEATURE-example-names
**Total:** 7 (0 critical, 4 major, 3 minor)

## Major

1. [ ] **spec-alignment-checker** | `scripts/test-v3-installed-consumer.py:262` | acceptance-criteria
   Frozen plan step 5 explicitly requires scripts/test-v3-installed-consumer.py; the validation brief requires its unresolved failures to remain blockers. CommandsTest.test_output_overflow_retires_descendants_and_bounds_log fails in both source and independently extracted final archive: immediate probe.bind at line 239 raises EADDRINUSE and reports that the descendant retained its socket after command failure. Required descendant-retirement verification is unsatisfied. Cleanup code and the test contract match the baseline; diagnostic passes establish no cause and do not close the original failures.
   *Recommendation:* Retain this required gate as failed/incomplete under coordinator verification/harness triage. Unsupported-root-cause repair requires clarification: bounded diagnostics established no cause. Do not prescribe a speculative process barrier, weaken the socket assertion, or substitute successful diagnostic trials. Closure requires valid evidence satisfying the unchanged cleanup contract or an explicit authorized baseline decision.

2. [ ] **spec-alignment-checker** | `test/installed/http3_flow.py:375` | acceptance-criteria
   Acceptance criterion "Installed shared/static example builds run successfully in both TLS lanes; every renamed installed consumer/client receives applicable compile/runtime verification" and plan step 8 require the unchanged bounded public inventory where no supported subset exists. TLS-on scripts/check-v3-public-http3.py fails the saturation RSS retirement <= warmed baseline + 1048576 assertion in the first shared/managed cell. Later scenarios, including renamed http3_semantics.py runtime, and the other three shared/static managed/external cells remain incomplete. Required installed H3 runtime coverage is unverified; this does not prove a rename-induced protocol regression.
   *Recommendation:* Retain the full TLS-on H3 matrix as incomplete under coordinator verification/H3 harness triage. Unsupported-root-cause repair requires clarification: baseline/canonical and precise-metric diagnostics did not reproduce or explain the failure. Preserve the memory bound and complete scenario/linkage/loop inventory. A speculative sampler change, retry-to-green, reduced receipt, skip or waiver cannot close this finding. Closure requires valid evidence satisfying the unchanged matrix or an explicit authorized baseline decision.

3. [ ] **test-quality-reviewer** | `scripts/check-v3-public-http3.py:119` | missing-test
   The required TLS-on public H3 matrix stops in its first shared/managed cell at the unchanged RSS retirement <= baseline + 1 MiB assertion in test/installed/http3_flow.py:375. Later scenarios including the renamed semantic-client runtime and all three remaining linkage/loop cells lack completed required verification. Plan step 8 and the acceptance criterion that every renamed installed client receives applicable runtime verification remain unfulfilled; diagnostic passes and syntax checks do not establish that coverage.
   *Recommendation:* Keep the full TLS-on H3 matrix incomplete under coordinator verification/H3 harness triage ownership pending decisive failure measurement or a justified narrow repair and valid complete-matrix closure. Retain the original RSS bound, normal allocator mode and full inventory. Do not substitute partial receipts or diagnostic reruns, increase thresholds or apply an unproven sampler repair.

4. [ ] **test-quality-reviewer** | `scripts/test-v3-installed-consumer.py:261` | missing-test
   Required installed-consumer verification has no successful completion: test_output_overflow_retires_descendants_and_bounds_log fails immediate probe.bind at line 239 with EADDRINUSE in both source and independent final-archive runs. This violates plan step 5 required contract verification. It is an unresolved verification gap, not proof of a rename regression or test flakiness; bounded diagnostics did not establish the cause.
   *Recommendation:* Keep the source/archive gate incomplete under coordinator verification/harness triage ownership pending decisive failure-boundary evidence or a narrowly justified and validated repair. Preserve the retirement assertion and original receipts. Do not retry to green, add sleeps, weaken the test or infer a repair from static cleanup code. Record any later explicit user criterion decision separately from a passing gate.

## Minor

5. [ ] **code-simplifier** | `examples/external_event_loop.cpp:27` | dependencies
   This translation unit uses std::string_view but has no std::string usage, so <string> is unnecessary.
   *Recommendation:* Remove #include <string>; retain <string_view> for the response body view.

6. [ ] **code-simplifier** | `examples/websocket_echo.cpp:6` | dependencies
   This translation unit does not use any declarations from <utility>.
   *Recommendation:* Remove the unused #include <utility> to keep the example's direct dependencies clear.

7. [ ] **housekeeper** | `README.md:1503` | documentation-stale
   The three historical-example links now target examples/legacy/, but their visible link labels still say examples/websocket_echo.cpp or examples/external_event_loop.cpp. The destination resolves, while the displayed path no longer matches the repository path.
   *Recommendation:* Update the three visible README link labels to include examples/legacy/ so the displayed path matches the destination.
