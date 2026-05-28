### TASK-047: Fire `request_received` and `body_chunk` (pre-handler short-circuit)

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** M

**Goal:**
Wire the two pre-routing, pre-handler phases that observe the inbound request. Both support short-circuit (`hook_action::respond_with(...)`). Closes feature request #273 (early 413 on oversize body); partially addresses #272 (delayed body processing — the `body_chunk` half).

**Action Items:**
- [x] Fire `request_received` from `webserver_impl::requests_answer_first_step` (relocated to `src/detail/webserver_body_pipeline.cpp` by the TASK-047 split) — after `mr->dhr.reset(new http_request(...))` so the `http_request` is populated, before the body read decision. Context: `request_received_ctx { http_request* request; steady_clock::time_point received_at; }` (the TASK-045 shape).
- [x] Fire `body_chunk` from `webserver_impl::requests_answer_second_step` (same file) — once per chunk delivered by MHD, before the chunk is appended to `mr->dhr` content / post-processor. Context: `body_chunk_ctx { http_request* request; std::span<const std::byte> chunk; std::uint64_t offset; bool is_final; }` (the TASK-045 shape; `offset` carries the running total of bytes already buffered).
- [x] Short-circuit handling: a non-pass hook return stashes the response into `mr->response_`, sets `mr->skip_handler = true`, and lets the existing finalize path queue it (new `if (mr->skip_handler)` early-return in `finalize_answer`). The resource handler is not invoked. The `body_chunk` short-circuit also calls `MHD_destroy_post_processor(mr->pp)` to free the 32 KB buffer so ASan stays clean.
- [x] Documented in the public header (`src/httpserver/hook_context.hpp`) that `body_chunk` fires from MHD worker threads at arbitrary granularity and hooks must be cheap (no blocking I/O, no per-chunk heap allocation).
- [x] Same `fire_*` pattern as TASK-046 (shared_lock → reserve(8) → copy → release → iterate → try/catch → `log_dispatch_error`). The `hook_action`-returning variant `fire_short_circuit_hooks_for_phase` lives in `hook_handle.cpp` next to the void variant.

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

**Status:** Done
