# Unworked Review Issues

**Run:** 2026-10-15 16:38:34
**Task:** FEATURE-example-names historical reconciliation
**Total:** 3 (0 critical, 0 major, 3 minor)

## Minor

1. [ ] **code-simplifier** | `examples/external_event_loop.cpp:27` | dependencies
   This translation unit uses std::string_view but has no std::string usage, so <string> is unnecessary.
   *Recommendation:* Remove #include <string>; retain <string_view> for the response body view.

2. [ ] **code-simplifier** | `examples/websocket_echo.cpp:6` | dependencies
   This translation unit does not use any declarations from <utility>.
   *Recommendation:* Remove the unused #include <utility> to keep the example's direct dependencies clear.

3. [ ] **housekeeper** | `README.md:1503` | documentation-stale
   The three historical-example links now target examples/legacy/, but their visible link labels still say examples/websocket_echo.cpp or examples/external_event_loop.cpp. The destination resolves, while the displayed path no longer matches the repository path.
   *Recommendation:* Update the three visible README link labels to include examples/legacy/ so the displayed path matches the destination.

## Historical reconciliation

This report supersedes the open-finding status in [the original naming report](feature-example-names_validation_90c8796511667be10eaf93675a2caf2d.md). The three minor observations above remain unworked and retain their original wording. The original report and all original validation artifacts, including `merge-policy-exception.json`, remain unchanged. Original run `90c8796511667be10eaf93675a2caf2d` remains historically failed/incomplete; its user-authorized merge exception remains a historical fact. This reconciliation records later closure and does not claim the original run passed.

The following four major findings are closed against implementation commit `e1fc8b6ace312f2cfcbcc365a41fafac1c8e8d99`:

| Historical finding | Later evidence satisfying the original invariant |
| --- | --- |
| `spec-alignment-checker-iter1-1` | Source and independently extracted archive installed-consumer gates each passed all 41 tests, including immediate socket reuse after output-overflow descendant retirement. |
| `test-quality-reviewer-iter1-1` | The same source/archive 41-test results satisfy the required installed-consumer contract coverage. |
| `spec-alignment-checker-iter1-2` | The complete TLS-on public HTTP/3 matrix passed shared/static × managed/external: 192 configuration checks, 64 basic scenarios and 204 nested scenarios, with both client identities in every cell. |
| `test-quality-reviewer-iter1-2` | The same complete matrix satisfies runtime coverage, including the semantic client; the authenticated archive bridge covers all 439 runtime inputs. |

Evidence is retained under `/home/etr/progs/libhttpserver/build/FEATURE-gate-investigation/`: `summary.json`, `frozen-owned-gates.json`, `frozen-source-installed-consumer.log`, `archive-contract.json`, `archive-installed-consumer.log`, `acceptance-on-public-http3/receipt.json` and `archive-runtime-bridge.json`. The source/archive logs each record 41 tests and `OK`; the archive command exits zero. The runtime receipt and archive bridge report `passed` and all four cells are complete. The unchanged 1048576-byte RSS bound, normal allocator mode and full scenario inventory were preserved.

Authentication is recorded in `/home/etr/progs/libhttpserver/.git/groundwork/validation/cdb4ee2aea69cc6a/256436568f42f1c8/groundwork-validation-2e691bd97f81348f27c2ce9a71379ce6/gate-authentication.json`; exact artifact hashes, original-preservation verification and the helper invocation are in its sibling `fixer-evidence-iter1.json`. The summary hash matches that authentication, indexed receipt hashes match the summary, and all four implementation-file hashes match the cited commit. No acceptance gates were rerun for this documentation repair. BSD/macOS/Windows execution remains CI/v3-PR owned and is not claimed here.
