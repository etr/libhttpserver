# Unworked Review Issues

**Run:** 2026-10-04 02:36:27
**Task:** TASK-123
**Total:** 7 (0 critical, 0 major, 7 minor)

## Minor

1. [ ] **code-quality-reviewer** | `src/detail/request_lifecycle.cpp:384` | complexity
   Inherited unchanged offender: dispatch_request: 51 NLOC, CCN 11, 354 tokens, 6 parameters, length 64. Official CCN_MAX=10 exceeded. Evidence: writer-complexity-fix-kzdocdw6/static-complexity.log and _task123-receipts/resume-final-static-base-comparison.json establish identical frozen-base e1fb7cb function metrics, ignoring only line offsets. Raw exit 1 / baseline_fail remains unworked; this is nonblocking debt, not a TASK-123 regression.
   *Recommendation:* In a separate cleanup, Extract the pre-route peer/disconnect/method admission checks into a focused helper, preserving request_completed on every early exit; replay lifecycle/hook tests. Keep the official threshold unchanged.

2. [ ] **code-quality-reviewer** | `src/detail/server.cpp:122` | complexity
   Inherited unchanged offender: native_server::impl::listen: 45 NLOC, CCN 11, 398 tokens, 0 parameters, length 57; base line 121/current line 122. Official CCN_MAX=10 exceeded. Evidence: writer-complexity-fix-kzdocdw6/static-complexity.log and _task123-receipts/resume-final-static-base-comparison.json establish identical frozen-base e1fb7cb function metrics, ignoring only line offsets. Raw exit 1 / baseline_fail remains unworked; this is nonblocking debt, not a TASK-123 regression.
   *Recommendation:* In a separate cleanup, Extract peer-policy seeding or config construction into a typed-result helper, preserving validation-before-bind and partial-listen teardown; replay native-server tests. Keep the official threshold unchanged.

3. [ ] **code-quality-reviewer** | `src/httpserver/body_reader.hpp:157` | duplication
   Inherited unchanged offender: 43 lines / 144 tokens duplicated with src/httpserver/response_writer.hpp:162. Official CPD_MIN_TOKENS=100 exceeded. Evidence: repair2-current-duplication.log and repair2-current-duplication-baseline-comparison.json establish exact normalized complete block/location/metric equivalence against frozen base e1fb7cb; combined block digest a018ad216f9cce17cee3889315cda52c8a7f74c4e6a9a8648e0e2d1ccb093dbe. Raw exit 1 / baseline_fail remains unworked; this is nonblocking debt, not a TASK-123 regression.
   *Recommendation:* In a separate cleanup, Consider sharing only the claim and witness-guarded executor resumption primitive, preserving distinct body_wake/body_room results and seam directions; replay cancellation/frame-lifetime tests. Keep the official threshold unchanged.

4. [ ] **code-quality-reviewer** | `src/httpserver/concurrency/task.hpp:532` | duplication
   Inherited unchanged offender: 26 lines / 133 tokens duplicated with src/httpserver/concurrency/task.hpp:615. Official CPD_MIN_TOKENS=100 exceeded. Evidence: repair2-current-duplication.log and repair2-current-duplication-baseline-comparison.json establish exact normalized complete block/location/metric equivalence against frozen base e1fb7cb; combined block digest a018ad216f9cce17cee3889315cda52c8a7f74c4e6a9a8648e0e2d1ccb093dbe. Raw exit 1 / baseline_fail remains unworked; this is nonblocking debt, not a TASK-123 regression.
   *Recommendation:* In a separate cleanup, Evaluate a shared frame-ownership helper while preserving typed/void promise and awaiter behavior; replay task core/race/cancellation/resume-lifetime tests. Keep the official threshold unchanged.

5. [ ] **code-quality-reviewer** | `src/httpserver/detail/io_poll_sys.hpp:407` | complexity
   Inherited unchanged offender: pollsys::accept_one: 36 NLOC, CCN 11, 225 tokens, 3 parameters, length 41. Official CCN_MAX=10 exceeded. Evidence: writer-complexity-fix-kzdocdw6/static-complexity.log and _task123-receipts/resume-final-static-base-comparison.json establish identical frozen-base e1fb7cb function metrics, ignoring only line offsets. Raw exit 1 / baseline_fail remains unworked; this is nonblocking debt, not a TASK-123 regression.
   *Recommendation:* In a separate cleanup, Extract platform error classification helpers, preserving would_block/closed_reset, peer extraction, and socket preparation; replay backend contracts on supported platforms. Keep the official threshold unchanged.

6. [ ] **code-quality-reviewer** | `src/httpserver/server/options.hpp:484` | complexity
   Inherited unchanged offender: valid_peer_pattern: 22 NLOC, CCN 15, 201 tokens, 1 parameter, length 24. Official CCN_MAX=10 exceeded. Evidence: writer-complexity-fix-kzdocdw6/static-complexity.log and _task123-receipts/resume-final-static-base-comparison.json establish identical frozen-base e1fb7cb function metrics, ignoring only line offsets. Raw exit 1 / baseline_fail remains unworked; this is nonblocking debt, not a TASK-123 regression.
   *Recommendation:* In a separate cleanup, Isolate CIDR prefix and mapped-address validation from literal/wildcard classification, preserving constexpr behavior and exact grammar; replay options/peer-policy tests. Keep the official threshold unchanged.

7. [ ] **spec-alignment-checker** | `src/httpserver/server/server.hpp:93` | specification-gap
   The public drain_ticket::wait comment still promises that request_stop()/stop() racing the wait 'drive it to completed' without an expiry qualification. TASK-123 now deliberately preserves deadline_expired once claimed, as documented by begin_drain at 176-178 and implemented by drain_scope::wait. A stop racing an already-expired wait cannot produce completed, so the two public lifecycle comments conflict with the truthful sticky-outcome policy required by PRD-V3N-REQ-032 and the accepted plan.
   *Recommendation:* Qualify the drain_ticket::wait comment (and the nearby drain_status overview) so stop yields completed only when all counted units unwind before expiry; a claimed expiry remains deadline_expired.
