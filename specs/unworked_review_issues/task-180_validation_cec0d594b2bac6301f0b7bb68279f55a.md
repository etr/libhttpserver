# Unworked Review Issues

**Run:** 2026-10-09 17:42:31
**Task:** TASK-180
**Total:** 2 (0 critical, 2 major, 0 minor)

## Major

1. [ ] **test-quality-reviewer** | `test/unit/resource_limits_test.cpp:284` | missing-test
   Both iterations give the child a body-buffer capacity of 1 while the coalesced tail requires 81 units. Admission checks the leaf first, so the parent_limit=true iteration also refuses at the child and never exercises parent-only pipeline refusal. The separate sibling/accounting regression still verifies ancestor charging and capacity reuse, so this gap is nonblocking under the review verdict rules.
   *Recommendation:* Give the child sufficient tail capacity in the parent-refusal scenario, and hold a sibling reservation that leaves the parent with insufficient available capacity; keep the existing low-child/high-parent scenario for child refusal.

2. [ ] **test-quality-reviewer** | `test/unit/resource_limits_test.cpp:95` | missing-test
   The new HTTP/1 stream-seat refusal branch (src/detail/connection_engine_request.cpp:90-91) is never exercised. This case admits one exchange with streams=1, checks its charge, then shuts it down; no H1 test attempts a second exchange below that exhausted shared scope. A regression that dispatches refused requests, omits the 503/close outcome, or leaks refused-head resources would still pass. TASK-180 requires configured stream limits to reject excess, and its frozen plan explicitly requires first-excess refusal, no appended sentinel dispatch, and reuse.
   *Recommendation:* Add a real H1 socket regression with two connections sharing streams=1: suspend the first handler, send an excess request plus a sentinel on the sibling, verify 503/terminal closure and zero excess handler dispatch, unchanged admitted usage and eventual refused-head release; retire the first exchange and verify a replacement request is admitted and completes. Cover HTTP/1.0 and HTTP/1.1 using the existing fixture.
