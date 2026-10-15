# Unworked Review Issues

**Run:** 2026-10-10 05:13:57
**Task:** TASK-185
**Total:** 3 (0 critical, 2 major, 1 minor)

## Major

1. [ ] **spec-alignment-checker** | `scripts/lib/v3_release_execution.py:131` | action-item
   The stage receipt recursively hashes package/evidence, including mutable configured build files, but normal_build() at lines 155-157 returns those same build directories to later suites. Retained attempt-05 package verification fails: test/examples/.deps/v3_tls_rotation-v3_tls_rotation.Po was recorded as 8 bytes / SHA-256 ddb1c8705176da86c285112f625f6a732bf7353b0a17835e8bcbc27c0e4243d9 and is now 52862 bytes / ac7d4603d612a03f654aa73331b7f839b616a3d6f10355aa5b37ae3701bcde57. The complete hash audit found 1144 mismatched record occurrences in that manifest (including duplicated envelope/receipt records); independent supplements and raw-native receipts had zero mismatches. Archive/source/provider/tool identities remain intact, but the recorded passing package envelope cannot now satisfy verify_receipt. This independently violates the action to retain exact release receipts and can prevent readiness after later suites succeed.
   *Recommendation:* Preserve immutable stage evidence separately from build directories reused by later gates, or run subsequent suites in distinct build derivatives, while retaining hashes for required production binaries, archives, headers, metadata, logs and tool identities. Add a contract that completes a package stage, executes a later native build/check stage, and then successfully re-verifies the original package receipt. Preserve the unsuccessful historical manifest; document its integrity failure rather than rewriting recorded hashes.

2. [ ] **spec-alignment-checker** | `specs/tasks/M13-v3-release/TASK-185.md:21` | acceptance-criteria
   The required all-v3.0-local-gates acceptance criterion is unmet. Frozen attempt-05 contains 18 stages: only package/http2/fuzz-off/fuzz-on report passed; 14 report failed; local_passed=false and tag_ready=false. Remaining actual local blockers after the documented adapter/normalizer supplements are H2 route-effect checks, H1/WebSocket handshakes, peer-policy timeout, ASan lifetime failures, TSan operator-new/delete build conflict, missing cookie_render parity witness, H3 semantic/drain deadline, public installed TLS/H2/H3/credential integration, and native throughput/latency benchmark infrastructure. The current-08 archive is focused evidence, not a complete passing release inventory.
   *Recommendation:* Retain unsuccessful local acceptance and In Progress status. Triage the listed protocol/sanitizer/parity failures to their existing owners and track public integration and native benchmark capability as separately owned blocked dependencies; do not implement unrelated product/benchmark infrastructure in TASK-185 or waive mandatory gates. Re-execute the affected required gates on one selected archive only after those dependencies resolve. Nonlocal platform checks remain CI/v3-PR owned.

## Minor

3. [ ] **test-quality-reviewer** | `test/integ/release_gate_runner_test.py:372` | test-validity
   test_pending_platforms_preserve_local_pass_but_block_tag omits schema_version and inventory, so check_tag_ready raises 'wrong release receipt schema' before it reaches the pending-platform check. A direct reproduction confirmed that rejection. The test would still pass if platform readiness enforcement regressed.
   *Recommendation:* Start from a valid complete manifest with verified local gate envelopes, set the required platform rows to pending (or remove them), retain the local_passed assertion, and assert the platform-specific failure message. Preserve the complete-manifest positive control.
