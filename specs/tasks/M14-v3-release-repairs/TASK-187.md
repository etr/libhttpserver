### TASK-187: Repair response-source and multipart coroutine lifetimes

**Milestone:** M14 - v3 local release readiness repairs
**Component:** Response ownership and multipart streaming
**Estimate:** M
**Branch policy:** Current `v3` → isolated task branch → validated merge into `v3` → cleanup.
**Goal:** Remove the retained `response_sources` and `forms_multipart_route` ASan lifetime failures while preserving explicit borrowed-memory leases and multipart cancellation.

**Workflow:** Follow `../v3-release-repair-workflow.md`; resolve TASK-187 with the Groundwork worktree-identity helper, require `base_branch=v3`, use C++20 RED/GREEN TDD, validation and finalization. Preserve TASK-185 and other worktrees.

**Action Items:**
- [x] Read the two frozen ASan logs under `/home/etr/progs/libhttpserver/.worktrees/TASK-185/build/task185-final-native-audit-05/asan-ubsan-off/`. Reproduce `response_sources/borrowed_destruction_releases_keeper_once` and `forms_multipart_route/read_disconnect_aborts_once` with stack lifetime checks enabled. Inspect `response_sources_test.cpp:384`, `body_sink_fake.hpp:121`, `forms_multipart_route_test.cpp:675`, `forms_multipart.cpp:276` and public signatures; identify temporary payload, lease, coroutine closure and reference-parameter lifetimes independently.
- [x] Add delayed-start and suspended-operation regressions for the demonstrated cases, asserting bytes, keeper release count, multipart abort count and cancellation result. Own data/limits that are documented by value, and keep explicitly borrowed resources alive by their documented lease. If the public lazy coroutine accepts a temporary value-like limits argument, take an owned snapshot before suspension rather than merely lengthening the existing test fixture lifetime. Document any justified API signature change.
- [x] Repair production or fixture code according to proven ownership, without copying borrowed data as a hidden workaround or disabling the failing checks. Preserve exactly-once source/sink cleanup, bounded buffering and replayable definitions.
- [x] Verify affected response/file/pipe/borrowed and multipart route/decoder/cancellation suites; update ownership documentation and installed consumer coverage for changed public signatures.

**Acceptance Criteria:**
- Both retained targets pass normal and ASan/nonrecovering UBSan TLS-off/on with no lifetime suppression.
- Delayed coroutine start, parked writes, disconnect and cancellation keep required inputs valid and release leases/abort begun multipart parts once; assertions execute and all relevant suites pass.
- C++20 public consumers, applicable TSan owner tests and Groundwork validation pass; evidence/tracking updated and task merged/cleaned.

**Dependencies:**
- Blocked by: TASK-186
- Blocks: TASK-188, TASK-185
**Related Requirements:** PRD-V3N-REQ-021, PRD-V3N-REQ-024, PRD-V3N-REQ-025, PRD-V3N-REQ-026, PRD-V3N-REQ-028, PRD-V3N-REQ-029, PRD-V3N-REQ-038.
**Related Decisions:** DR-V3-003, DR-V3-005, DR-V3-006.
**Status:** Complete

**Implementation evidence:** [TASK-187 implementation evidence](../../architecture/v3/TASK-187-implementation-evidence.md). Required affected normal/ASan TLS-off/on suites and TSan owner targets pass, as do 30 staged public consumer executions. Groundwork validation passed in one iteration with eight approvals, zero fixes and zero unworked findings (run `042c2872ddd35da2da0b750e793b21e4`). Implementation artifacts are retained at `build/task187-evidence/implementation-ad76a561/` in the primary worktree with a SHA256 archive manifest; relative implementation build/debug paths resolve beneath that archive. An extra intermittent unchanged task_race TSan finding remains open for separate caller-owned repair before TASK-185 final gate.
