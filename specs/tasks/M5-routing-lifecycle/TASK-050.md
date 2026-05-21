### TASK-050: Fire `after_handler` (post-handler short-circuit), `response_sent`, `request_completed`; wire `log_access` alias

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Hook bus
**Estimate:** M

**Goal:**
Wire the three tail-end phases. `after_handler` is the post-handler short-circuit (it can REPLACE the in-flight response). `response_sent` is the observation point that finally has the response, status, byte-count, and timing — the data #281 and #69 have been asking for. `request_completed` is the unconditional final hook. Convert `log_access` into the documented alias.

**Action Items:**
- [ ] Fire `after_handler` between `dispatch_resource_handler` and `materialize_and_queue_response` in `webserver_impl::finalize_answer` (`webserver.cpp:2444` → `webserver.cpp:2449`). Context: `after_handler_ctx { const http_request& req; http_response& resp; }` — mutable response reference so a hook can `resp.with_header(...)` in place. Short-circuit semantics: a hook returning `hook_action::respond_with(r2)` REPLACES `mr->response_` with `r2` and skips remaining hooks at the phase. Subsequent hooks at later phases (`response_sent`, `request_completed`) still fire on the replaced response.
- [ ] Fire `response_sent` immediately after the `MHD_queue_response` call in `webserver_impl::materialize_and_queue_response` (`webserver.cpp:2421`), BEFORE `MHD_destroy_response`. Context: `response_sent_ctx { const http_request& req; const http_response& resp; int status; std::size_t bytes_queued; std::chrono::nanoseconds elapsed; }`. `elapsed` is measured from `answer_to_connection`'s first invocation for this `mr` (timestamp captured in `modded_request` at TASK-045 time).
- [ ] Fire `request_completed` from `webserver_impl::request_completed` (`webserver.cpp:1253`), BEFORE the `delete static_cast<detail::modded_request*>(*con_cls)` step (otherwise the context fields would dangle). Context: `request_completed_ctx { const http_request& req; const http_response* resp; bool succeeded; }`. `resp` is nullable — on early failures (`accept_decision` rejection, etc.) there may be no response object.
- [ ] **Alias: `log_access(fn)`.** Internally register a `response_sent` hook that formats the request line as today and invokes `fn(line)`. Re-registration replaces. Doxygen notes it as an alias and recommends `add_hook(hook_phase::response_sent, ...)` for users who want the structured context (status, bytes, timing — what #281 and #69 actually asked for).
- [ ] `examples/clf_access_log.cpp`: a Common-Log-Format access logger as a `response_sent` hook, demonstrating that the long-requested CLF / `time-taken` format is now writable in user code without a library change. Closes issues #281 and #69 from the user-facing-doc perspective.

**Dependencies:**
- Blocked by: TASK-045
- Blocks: TASK-051, TASK-052

**Acceptance Criteria:**
- New integ test `hooks_after_handler_replaces_response`: an `after_handler` hook returning `hook_action::respond_with(r2)` is observed on the wire; the response from the resource is NOT.
- New integ test `hooks_after_handler_mutates_response_in_place`: an `after_handler` hook returning `pass()` after calling `ctx.resp.with_header("X-Foo","bar")` produces a response on the wire that carries the header.
- New integ test `hooks_response_sent_carries_status_bytes_timing`: a `response_sent` hook sees `status == 200`, `bytes_queued == body.size()`, `elapsed > 0`.
- New integ test `hooks_request_completed_fires_on_early_failure`: a `request_received` hook short-circuits to a 413; `request_completed` still fires with `succeeded == true` and the 413 response object visible.
- `examples/clf_access_log.cpp` builds and produces CLF lines on stdout when exercised by curl.
- The existing `log_access` tests pass unchanged.
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HOOK-REQ-002, PRD-HOOK-REQ-004, PRD-HOOK-REQ-005, PRD-HOOK-REQ-009
**Related Decisions:** DR-012, §4.10
**Resolves:** Issues #281, #69

**Status:** Not Started
