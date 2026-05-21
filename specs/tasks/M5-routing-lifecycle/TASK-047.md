### TASK-047: Fire `request_received` and `body_chunk` (pre-handler short-circuit)

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** M

**Goal:**
Wire the two pre-routing, pre-handler phases that observe the inbound request. Both support short-circuit (`hook_action::respond_with(...)`). Closes feature request #273 (early 413 on oversize body); partially addresses #272 (delayed body processing — the `body_chunk` half).

**Action Items:**
- [ ] Fire `request_received` from `webserver_impl::requests_answer_first_step` (`webserver.cpp:2010`) — after `mr->dhr.reset(new http_request(...))` so the `http_request` is populated, before the body read decision. Context: `request_received_ctx { http_request& req }`. (Mutable ref so a hook can adjust per-request state like content-size limit if a follow-up adds that knob.)
- [ ] Fire `body_chunk` from `webserver_impl::requests_answer_second_step` (`webserver.cpp:2034`) — once per chunk delivered by MHD, before the chunk is appended to `mr->dhr` content / post-processor. Context: `body_chunk_ctx { http_request& req; std::span<const char> bytes; std::size_t total_bytes_received; }`.
- [ ] Short-circuit handling: if any hook returns `hook_action::respond_with(r)`, stash `r` into `mr->response_`, mark `mr` as "skip-to-finalize", and let the existing finalize path queue it. The resource handler is not invoked. For `body_chunk` specifically, also drop any in-flight post-processor (`MHD_destroy_post_processor`) to free its buffer.
- [ ] Document in the public header that `body_chunk` fires from MHD worker threads at arbitrary granularity — chunks may be byte-level on slow networks. Hooks must be cheap (no I/O, no allocation in the hot path) or risk back-pressuring the connection.
- [ ] Same `fire_*` pattern as TASK-046 (shared_lock → copy → release → iterate).

**Dependencies:**
- Blocked by: TASK-045
- Blocks: TASK-052

**Acceptance Criteria:**
- New integ test `hooks_request_received_short_circuit`: a `request_received` hook returning `hook_action::respond_with(http_response::empty().with_status(413))` on `Content-Length > 1MB` aborts the upload before any body bytes are consumed. Verified by registering a downstream `body_chunk` hook that asserts it was never called. Demonstrated as the solution to #273 in `examples/early_413.cpp`.
- New integ test `hooks_body_chunk_observes_progress`: a `body_chunk` hook accumulates `bytes.size()` across firings; total equals the request body length when the upload completes.
- A `body_chunk` short-circuit drops the post-processor without leaking (verified under ASan in the existing `build-type: asan` CI matrix entry).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-002, PRD-HOOK-REQ-003, PRD-HOOK-REQ-005
**Related Decisions:** DR-012, §4.10
**Resolves:** Issue #273. Partially addresses issue #272 (observation half only — body-buffer steal remains a v2.1 candidate).

**Status:** Not Started
