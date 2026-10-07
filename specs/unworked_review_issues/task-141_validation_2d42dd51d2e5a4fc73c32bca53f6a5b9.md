# Unworked Review Issues

**Run:** 2026-10-07 06:28:54
**Task:** TASK-141
**Total:** 4 (0 critical, 1 major, 3 minor)

## Major

1. [ ] **code-quality-reviewer** | `src/detail/http2_stream_flow.cpp:140` | error-handling
   encode_data() checks connection.failure() only before its selection loop. When frame_data() fails the active output reservation at lines 125-126, it terminates the connection but returns false, so the loop attempts another stream. A bounded current-library probe with response_queue_bytes=42000 and completed streaming handlers staging 16384 bytes on stream 1 and 1 byte on stream 3 captures SETTINGS, ACK, HEADERS(1), HEADERS(3), newly selected DATA(3), GOAWAY. Thus a resource-limit failure still initiates fresh semantic output and debits sibling send windows after termination; this is not preservation of output exposed before failure. The existing allocation-failure test has one stream and cannot catch the continued selection.
   *Recommendation:* Stop selecting frames immediately when frame_data() or encode_trailers() sets connection.failure(), allowing output() to publish the terminal control in that pump. Add a deterministic two-stream regression with differing queued payload sizes and a budget that refuses the first frame but admits the second; assert no new sibling DATA/trailers are framed after the failure, while pre-exposed output remains immutable.

## Minor

2. [ ] **code-simplifier** | `src/detail/http2_response_stream.cpp:37` | redundant-validation
   prepare_response first calls http2_content_length, which already validates decimal syntax, overflow and equal numeric duplicates, then re-parses every nonstreaming Content-Length through valid_response_lengths/canonical_length. After successful numeric parsing, the second helper contributes only the zero-length rule for nonmetadata responses. Keeping two parsers and duplicate equality implementations makes this rule harder to trace and leaves approximately 17 lines of now-redundant helper code in the shared state header.
   *Recommendation:* Use the already parsed length for the nonstreaming/nonmetadata zero-body check, then remove valid_response_lengths and canonical_length if their only call remains this one. Preserve the existing HEAD/304 metadata and no-content status checks.

3. [ ] **code-simplifier** | `src/httpserver/detail/http2_request_state.hpp:141` | unused-accounting-state
   connection_credit_queued is incremented when WINDOW_UPDATE is queued and decremented when exposed, but it is never queried, asserted or used in any receive-window calculation. In contrast, per-stream credit_queued participates in the grant gap. The unused connection counter adds a second apparent source of truth that a maintainer must reconcile despite having no behavioral role.
   *Recommendation:* Remove connection_credit_queued and its two updates, leaving consumed reset on successful enqueue and receive_window increased on first exposure. If the counter is intended to enforce an invariant, document and actually check that invariant instead of retaining write-only bookkeeping.

4. [ ] **performance-reviewer** | `src/detail/http2_request_engine.cpp:32` | algorithmic-complexity
   Every feed call invokes sync_settings(), whose loop in http2_stream_flow.cpp:14 visits every live stream and performs zero-delta window adjustments even for partial DATA, HEADERS, and other input that cannot change settings. With S parked streams and K input fragments this adds O(K*S) stream visits; the default S=128 and hierarchical stream admission keep the cost bounded, so this is nonblocking.
   *Recommendation:* Avoid the full synchronization pass for ordinary non-SETTINGS input, for example by running it only when an applicable SETTINGS event completes. Preserve acknowledged receive-window changes and intermediate repeated initial-window overflow checks. Benchmark fragmented input with the maximum admitted live-stream count before prioritizing this optimization.
