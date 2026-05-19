### TASK-038: Sanitizer-clean tests for `http_response` move semantics

**Milestone:** M6 - Release Readiness
**Component:** Test infrastructure
**Estimate:** M

**Goal:**
Verify all four `http_response` move cases are sanitizer-clean — the highest-bug-risk area in v2.0 per AR-004.

**Action Items:**
- [x] Write `test/unit/http_response_move_sanitizer_test.cpp` covering:
  - move-construct: inline source → destination (placement-new path)
  - move-construct: heap source → destination (pointer swap path)
  - move-assign: inline ↔ inline (4-case)
  - move-assign: inline ↔ heap (4-case)
  - move-assign: heap ↔ inline (4-case)
  - move-assign: heap ↔ heap (4-case)
- [x] Each case constructs an `http_response`, moves it through the operation, and exercises read accessors on the destination + asserts the source is in a valid moved-from state.
- [x] Run under AddressSanitizer + UndefinedBehaviorSanitizer in CI. (Fixed pre-existing `CXXLAGS` -> `CXXFLAGS` typo in `.github/workflows/verify-build.yml` so C++ TUs are actually instrumented under the asan/msan/lsan/tsan/ubsan matrix entries; the runtime libs were linked before but instrumentation was not compiled into the .o files.)
- [x] Add a synthetic body kind that exceeds 64 B (heap-fallback path) to cover the heap branch even if no current production body needs it. (`fat_body` in the new TU, sized to 128 B + counter pointer, placed into a response through the existing `http_response_sbo_test_access` friend hook.)

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

**Status:** Done
