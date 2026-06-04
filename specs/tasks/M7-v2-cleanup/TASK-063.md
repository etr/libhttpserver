### TASK-063: Honor or remove `http_response::pipe` `size_hint` parameter

**Milestone:** M7 - v2 Cleanup
**Component:** `http_response::pipe`
**Estimate:** S

**Goal:**
`pipe(int fd, std::size_t size_hint = 0)` exposes `size_hint` in the public API but the dispatch path ignores it; a test (`test/unit/http_response_factories_test.cpp:253-266`) pins "accepted-but-ignored" as the contract. Decide whether to honor the hint (sizing the streaming buffer, `Content-Length` synthesis, or kernel hint via `posix_fadvise`) or remove it from the signature.

**Action Items:**
- [ ] Decide one of: (a) honor `size_hint` as the streaming chunk size and/or `Content-Length`, or (b) remove the parameter and the pinning test.
- [ ] If (a): wire `size_hint` into `detail::body_pipe` (or wherever the pipe body is materialised) and add a `Content-Length` header when the hint is nonzero and finite. Add a real integration test that pins the byte-count emitted on the wire matches the hint when the underlying fd produces exactly that many bytes.
- [ ] If (b): drop `size_hint` from the public `pipe(int fd)` signature in `src/httpserver/http_response.hpp:159-161`, drop the corresponding default-arg in `src/http_response.cpp:409`, and convert the "accepted-but-ignored" test in `test/unit/http_response_factories_test.cpp:253-266` into a compile-fail or call-site update.
- [ ] Update RELEASE_NOTES.md "Migration notes" if the signature changed (binary/source incompatibility).

**Dependencies:**
- Blocked by: None
- Blocks: None

**Acceptance Criteria:**
- `pipe(int fd, …)` has a documented, tested contract — either honors `size_hint` or does not expose it.
- No accepted-but-ignored parameter survives on the public API.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-RSP-REQ-001 (factory by value), PRD §2 API minimalism
**Related Decisions:** None new

**Status:** Backlog
