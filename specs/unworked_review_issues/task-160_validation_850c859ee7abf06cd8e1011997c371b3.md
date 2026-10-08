# Unworked Review Issues

**Run:** 2026-10-08 09:42:37
**Task:** TASK-160
**Total:** 2 (0 critical, 0 major, 2 minor)

## Minor

1. [ ] **code-simplifier** | `src/httpserver/detail/http3_connection.hpp:42` | clarity
   Request sequencing is represented as unsigned request_stage with implicit values 0, 1 and 2. admit_request compares those numbers and increments on HEADERS, while clean_terminal relies on the zero value. The intended initial/body/trailers states therefore require reconstructing three separate operations.
   *Recommendation:* Optionally replace the numeric stage with a private enum naming initial_headers, body and trailers, and use explicit transitions while preserving the existing admission and terminal behavior.

2. [ ] **code-simplifier** | `src/httpserver/detail/http3_connection.hpp:72` | unused-state
   The newly added qpack_encoder encoder_ member is never read or invoked in the TASK-160 implementation. Outgoing bootstrap is produced from fixed prefixes, so this unused codec member and its include imply an encoding responsibility the connection does not yet exercise.
   *Recommendation:* Optionally defer the unused encoder_ member and qpack_encoder.hpp include until outgoing field encoding needs them; retain the current static prefixes and decoder ownership.
