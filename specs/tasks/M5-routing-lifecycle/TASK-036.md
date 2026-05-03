### TASK-036: Handler return-by-value dispatch cutover

**Milestone:** M5 - Routing, Lifecycle, Builder & Features
**Component:** Dispatch path
**Estimate:** M

**Goal:**
Wire the new handler-return-by-value contract end-to-end through the dispatch path: lambdas return `http_response` by value (TASK-025), `http_resource::render_*` returns `http_response` by value (TASK-022), and `webserver_impl`'s dispatch moves the value into MHD via `body_->materialize(...)`.

**Action Items:**
- [ ] Update the internal dispatch function signature inside `webserver_impl` to receive `http_response&&` (or accept by value and move).
- [ ] In the dispatch path, after the handler returns: enqueue the response, call `body_->materialize(...)` to obtain `MHD_Response*`, hand it to MHD, then keep the `http_response` value alive until `MHD_RequestTerminationCode` (so deferred bodies' producer callable lives long enough — DR-010).
- [ ] Remove any v1 code path that wrapped responses in `shared_ptr<http_response>` or `unique_ptr<http_response>` for handler return; remove now-dead helpers.
- [ ] For deferred responses, attach the `http_response` to the connection state so `request_completed` destroys it (per §5.3, DR-010).

**Dependencies:**
- Blocked by: TASK-022, TASK-025, TASK-027, TASK-031
- Blocks: TASK-038, TASK-040

**Acceptance Criteria:**
- `auto h = [](const http_request&) { return http_response::string("hi"); };` registered via `on_get` produces a 200 with body "hi".
- A class subclassing `http_resource` with `http_response render_get(const http_request&) override` produces the same.
- For a deferred response, the producer callable lives until `request_completed` fires (verified by an explicit test that puts a destruction-tracking object in the callable's captures).
- Typecheck passes.
- Tests pass.

**Related Requirements:** PRD-HDL-REQ-001, PRD-RSP-REQ-007
**Related Decisions:** DR-004, DR-010, §5.3

**Status:** Not Started
