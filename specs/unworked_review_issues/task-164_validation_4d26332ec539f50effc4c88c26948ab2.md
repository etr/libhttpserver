# Unworked Review Issues

**Run:** 2026-10-08 16:50:21
**Task:** TASK-164
**Total:** 4 (0 critical, 3 major, 1 minor)

## Major

1. [ ] **code-quality-reviewer** | `src/detail/quic_connection_identity.cpp:121` | logic-error
   A NEW_CONNECTION_ID first observed below the retirement floor is retired reliably but its CID/reset token is never stored, and this branch also skips cross-sequence conflict checks. At frozen HEAD 5f44f156, a bounded probe sends sequence 10/retire_prior_to 10 with CID90/token90, late sequence 1/retire_prior_to 0 with CID91/token91, then sequence 11/retire_prior_to 10 reusing CID91/token91. All three return ok and two peer CIDs remain active (probe output: high=0 late=0 reused=0 active=2). The last frame should be rejected under the plan/evidence contract that retired CID/token history remains owned and reuse is rejected. Existing retired-history tests only cover IDs learned while still active, so they miss this ordering.
   *Recommendation:* Check retained cross-sequence CID/token conflicts for both branches and retain each newly observed late retired CID/token as an inactive record within the existing cumulative history bound before completing retirement handling. Add regression cases for a late retired ID followed by CID or token reuse, contradictory repeats of that late sequence, and late retired history exhaustion. Reproducer: /tmp/task164-quality-cid-probe.cpp, linked against build/off/src/.libs/libhttpserver_v3core.a.

2. [ ] **security-reviewer** | `src/detail/quic_connection_identity.cpp:122` | input-validation
   NEW_CONNECTION_ID values first observed below new_floor are retired without recording their sequence/CID/reset token or checking the cumulative history bound. A task-local probe against frozen HEAD first set sequence/retire_prior_to=100, ACKed the retirement, then accepted 40 distinct sequences below that floor despite max_cid_history=32. It also accepted a changed CID/token for previously seen retired sequence 1 and subsequently accepted sequence 101 reusing sequence 1's retired CID/token. This violates TASK-164's retained bounded CID history and rejection of historical CID/token conflicts. Active tables remain fixed, and no authentication or output-redirection bypass was demonstrated. Supporting evidence: evidence: /tmp/task164-security-history.cpp and /tmp/task164-security-history: below-floor unique records accepted=40 max_history=32; same retired sequence with changed CID/token accepted=1; retired CID/token reused under active sequence accepted=1
   *Recommendation:* Validate historical CID/token conflicts and retain an inactive copied record for every first-seen peer CID, including below-floor arrivals, before retaining its retirement. Enforce max_cid_history for those records and preserve them after retirement ACKs. Add regressions for below-floor cumulative exhaustion, conflicting repeat of a below-floor sequence, and reuse of its CID/token under a new active sequence. RFC 9000 identity rules: https://www.rfc-editor.org/rfc/rfc9000.html#section-19.15.

3. [ ] **test-quality-reviewer** | `test/unit/quic_connection_identity_test.cpp:98` | missing-test
   TASK-164's bounded CID lifecycle requirement and frozen plan explicitly require active-limit calculation and historical-bound coverage, but the test named peer_duplicate_conflict_sparse_sequence_active_limit_and_retirement_floor retains only one active peer CID. The packet-order case reaches three; no test reaches four and rejects the fifth. Local issuance never tests refusing an additional CID at the default/negotiated peer limit, and peer-history tests stop at 16 records rather than exhausting the 32-record cumulative limit. Removing the relevant active-limit checks or changing peer-history admission would therefore evade the current boundary assertions, leaving the protocol/resource bounds without regression protection.
   *Recommendation:* Add focused state tests for local issuance at the default limit and a negotiated/clamped limit; accept four active peer CIDs and reject a fifth without retirement; and fill peer cumulative history while completing retirements, then assert the next new record returns capacity while duplicate retained records still behave correctly. Assert observable refusal and unchanged routes/counts rather than private storage.

## Minor

4. [ ] **code-simplifier** | `src/detail/quic_connection_identity.cpp:126` | code-structure
   The loop skips the existing CID record through a pointer comparison against a conditional nullptr expression, which obscures the simple iterator condition.
   *Recommendation:* Use `if (existing != peer.begin() + peer_used && &c == &*existing) continue;` so the end check and record skip are explicit.
