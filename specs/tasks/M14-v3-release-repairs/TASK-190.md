### TASK-190: Repair HTTP/2 conformance route-effect ordering

**Milestone:** M14 - v3 local release readiness repairs
**Component:** HTTP/2 engine and conformance corpus
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Resolve six retained conformance checks across five named transcripts without weakening stream errors or route semantics.

**Workflow:** Follow `../v3-release-repair-workflow.md`; helper-resolve TASK-190, require `base_branch=v3`, systematic cause analysis and RED/GREEN, then Groundwork validation/finalization.

**Action Items:**
- [x] Reproduce `http2_engine_conformance/rfc9113_engine_transcripts` failures for `content-length-data`, `malformed-trailer`, `closed-data`, `stream-window-overflow`, `half-closed-head` (47/52 transcripts currently complete). Trace protocol events versus scheduled header-time handler execution, resets and cancellation across executor batching and all fragmentation splits.
- [x] Establish whether corpus route expectations or engine behavior violate the documented header-time decision contract. Add focused failing regression for each distinct cause, asserting route observations, terminal scope/code/count, compression state and resource release. A handler may validly observe headers before a later malformed body; an expectation change requires specification-backed evidence and a regression, never blanket acceptance of fewer route checks.
- [x] Repair minimal engine scheduling/lifetime/error handling or demonstrably invalid oracle; preserve strict RFC 9113 error scope and suffix/HPACK handling after stream resets.
- [x] Run all corpus fragmentation variants, stream/body/response/WebSocket/drain suites and existing pinned h2spec/independent-client lane. Keep the exact h2spec exclusion inventory and named corpus requirements unchanged.

**Acceptance Criteria:**
- All 52 engine transcripts and existing engine tests pass with every route-effect check executed, under normal and ASan/nonrecovering UBSan TLS-off/on.
- Fragmentation/executor batching do not change the specified route admission/cancellation contract or wire error; no reservation leaks.
- Pinned H2 independent-client/conformance gate, relevant C++20 tests and Groundwork validation pass; merge/cleanup completed.

**Dependencies:**
- Blocked by: TASK-189
- Blocks: TASK-191, TASK-185
**Related Requirements:** PRD-V3N-REQ-005, PRD-V3N-REQ-006, PRD-V3N-REQ-009, PRD-V3N-REQ-023, PRD-V3N-REQ-025, PRD-V3N-REQ-037.
**Related Decisions:** DR-V3-001, DR-V3-003, DR-V3-006.
**Status:** In Progress
