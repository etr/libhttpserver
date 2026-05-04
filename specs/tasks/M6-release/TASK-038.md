### TASK-038: Sanitizer-clean tests for `http_response` move semantics

**Milestone:** M6 - Release Readiness
**Component:** Test infrastructure
**Estimate:** M

**Goal:**
Verify all four `http_response` move cases are sanitizer-clean — the highest-bug-risk area in v2.0 per AR-004.

**Action Items:**
- [ ] Write `test/http_response_move_sanitizer.cpp` covering:
  - move-construct: inline source → destination (placement-new path)
  - move-construct: heap source → destination (pointer swap path)
  - move-assign: inline ↔ inline (4-case)
  - move-assign: inline ↔ heap (4-case)
  - move-assign: heap ↔ inline (4-case)
  - move-assign: heap ↔ heap (4-case)
- [ ] Each case constructs an `http_response`, moves it through the operation, and exercises read accessors on the destination + asserts the source is in a valid moved-from state.
- [ ] Run under AddressSanitizer + UndefinedBehaviorSanitizer in CI.
- [ ] Add a synthetic body kind that exceeds 64 B (heap-fallback path) to cover the heap branch even if no current production body needs it.

**Dependencies:**
- Blocked by: TASK-009, TASK-036
- Blocks: None

**Acceptance Criteria:**
- ASan + UBSan run reports no errors across all 4 move cases.
- Test runs in `make check`.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-001, PRD-RSP-REQ-007
**Related Decisions:** DR-005, AR-004, §9 testing item 3

**Status:** Not Started
